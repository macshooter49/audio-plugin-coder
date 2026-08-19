# WIDEN — FINDINGS

**fb420.** What was measured, what surprised me, what I cut, what I could NOT prove.
Harness: `widen_cert.cpp` -> **127 PASS / 0 FAIL**, full output in `widen_cert_fb420.log`.

```
clang++ -O2 -std=c++17 \
  -I plugins/TerrainInstrument/Tests/shim \
  -I plugins/TerrainInstrument/Source \
  -I plugins/TerrainInstrument/Design/fx4/widen \
  plugins/TerrainInstrument/Design/fx4/widen/widen_cert.cpp -o /tmp/widen_cert && /tmp/widen_cert
```

---

## 1. THE HEADLINE MEASUREMENTS

| Claim | Number | Where |
|---|---|---|
| **The Detune knob reads TRUE CENTS, independent of Rate** — the gap the bible identified in Serum | peak cents **140.5 … 140.8** across a **12x** rate span (0.139 -> 5.569 Hz): **0.17 % spread** | §E |
| **The Dimension triangle holds its detune** | **99.2 %** of samples at \|c\| > 0.8·peak; **0.08 %** in the ±10 % dead zone | §D |
| **…and the A/B proves the detector can see the alternative** | the sine (`Wobble`) reads **0.435** lobe mass and **0.0627** zero dwell against closed-form arcsine predictions of **0.410** and **0.064** | §D |
| **Width goes past mono-destruction, with substance** | every Type: corr **-1.000**, mono fold **-128 to -144 dB**, output still within **-8.4 … +2.7 dB** of the Width-50 % level | §R |
| **`Bands` mono fold is spectrally EXACT** | mean deviation **0.000 dB at every Amount**, including past g = 1 where the quiet channel's band gain goes negative | §J |
| **Mix 1.0 = fully wet, zero dry** | dry residual **-142.1 to -142.4 dB**, with a **-3.01 dB** control at Mix 0.5 proving the probe can see dry at all | §H |
| **Feedback is a wall** | **+14.9 dB** of sustained density, taper calibrated so every quarter of the knob moves | §R, §F |
| **Every Type pair is distinct** | perceptual L2 >= **2.43** (1.0 = one audible step); closest pair still **3.1x** apart on its stated discriminator | §C |
| **Every one of the 48 Characters changes physics** | 0 weak cells; weakest **0.60** against a 0.35 threshold | §G |
| **60 s of full-drive white noise, every Type** | no NaN, peak <= **1.63** | §K |
| CPU, worst Type | `Shift` **23.2 us/block** = **0.87 %** of one core; x6 = 5.2 % | §M |

---

## 2. THE BUGS THE HARNESS FOUND — every one of them was in MY code, not in the probe

Listed in the order they were found. All are fixed and commented at the point of the error.

### 2.1 `Blur` and `Bands` were guarded on the wrong index — two Types did nothing
`recalc()` computed the allpass table under `if (type_ == 2)` and the band tree under
`if (type_ == 3)`. Those are **family** indices; the **Type** indices are 4 and 5. Result: `Blur`
ran with both cascades at identical coefficients (correlation **+1.000 exactly**, i.e. no effect at
all) and `Bands` ran with every crossover coefficient at zero (a passthrough). Both Types read as
"working" in casual listening because the dry-ish wet still came through.
*What caught it:* the §C fingerprint. `corr +1.000` on a **widener** is not a subtle number.

### 2.2 The retrig residual had the wrong SIGN and DOUBLED the modulation
`fireRetrig()` stores `res = sin(phi_old)` and forces `phi = 0`, so the residual exists to **hold**
the old read offset while the slew cap bleeds it away. The code did `s -= res_[v]`. At the retrig
sample the sine is ~0 and the read position therefore stepped by `-dep*sin(phi_old)` — the exact
negative of where it was, i.e. a **2x jump in the delay excursion**. Measured: peak second-difference
**8.14e-02**, **528x** the static floor, an exact factor-of-2 step visible in the raw samples.
*What caught it:* the click gate. Nothing else would have; the effect still "worked".

### 2.3 The Type-swap dip was 20 dB too shallow — and the real bug was 13 ms downstream
Two separate faults in one symptom.
1. The dip floor was 0.02 with a 0.05 trigger (-26 dB). A `Field` change can swap `Side Only`
   (pure side — tiny on a near-mono wet) for `Collapse` (pure mid — large): an **11x level change**,
   measured. 5 % of an 11x change is still a step. Floor moved to **0.005 (-46 dB)**.
2. The residue was worse and stranger: a Type swap produced a **3.17e-02** transient (250x the
   floor) *five blocks after* the swap, when the dip had already recovered to 0.28. Bisecting by
   Character found it: the `No Compander` Character dropped it to 1.02e-03. **`Twin` was writing
   companded, pre-emphasised audio into the SHARED ring buffer.** So the buffer's *content* stepped
   at the swap sample, and that step emerged one delay-time later — after any dip has recovered.
   No dip length fixes a discontinuity in stored signal.
   **Fix, structural:** the compander moved to the wet OUTPUT stage; the ring is now
   Type-independent, always. See §4.1 for why nothing was lost.

### 2.4 `Octave Bloom`, `Half Time` and `Vocal Four` were dead Characters
- `Octave Bloom` addressed voices `v >= 6`. The **default voice count is 6**, so it addressed
  nothing. Distance measured **exactly 0.00**. Now it addresses the top two **live** voices.
- `Half Time` set `baseMul = 0.5`, but the base is depth-driven (§3.1) so the growth floor swallowed
  it entirely — **0.00**. Re-voiced as `Mode Two` = a 2.2x clock, which under the constant-cents law
  *is* the shorter-delay mode Arturia measured.
- `Vocal Four`'s coefficient row was **character-for-character identical** to `Vocal Two`'s — a
  copy-paste that no amount of listening would have located. Replaced with `Wide Room`.

### 2.5 The `Offset` law put unity at 0.79, not at the knob centre
`0.25 * 10^t` reaches 1.0 at t = 0.79. The shipped default (0.5) was therefore running **every base
delay at 0.79x**, the modulation depth was clamping against it, and the constant-cents law delivered
**112 cents where it should deliver 130**. Now `0.25 * 16^t` — symmetric in octaves about 1.0, and
reaching 4x instead of 2.5x.
*What caught it:* the §E rate-independence gate, which failed at 20 % spread.

### 2.6 The achieved-cents read-out was under-reporting by the per-voice rate ratio
The depth is solved with `rHz * rho_v` (the scattered fan) and the achieved cents were read back
with `rHz` alone — off by exactly rho. Reported 109.9 where the law delivers 130.

### 2.7 The equal-power pan law cost 3 dB per channel that nothing put back
`vNorm = 1/sqrt(sum g^2)` normalises the voice sum's total power; equal-power panning then splits
it, so each channel receives half. Every voice Type was **-3.97 dB** at Mix 1.0. Fixed with the
sqrt(2).

---

## 3. WHERE I DIVERGED FROM THE BIBLE, AND WHY

### 3.1 THE BASE DELAY GROWS TO THE CENTS. The bible clamps the cents to the base.
`HYPER-BUILD-BIBLE.md` §3.1 states `A_v <= 0.9 * base_v` and derives the consequence honestly:
±120 cents "is reachable only above ~1.3 Hz on the 9.7 ms voice". Built that way and measured, the
Detune knob **dies a third of the way up at every musically useful Rate** — 28 cents at 0.3 Hz on
the default fan. That is a dead knob (house law 1) and a failed R11 ceiling.

**We do the opposite:** `base_eff = max(base_v * Offset, A/0.85 + 1.5 ms)`. The read head still
never approaches the write head; the cents stay honest at every Rate; the only cost is that slow +
deep settings sit further back in time — which is what a doubler does anyway. `A` is hard-capped at
110 ms, and above that cap the **achieved** cents fall and are published in `viz().voiceCents`.

### 3.2 The `Dimension` Type is called `Twin`, and the `Ensemble` Type is cut
`Dimension` is Serum's own menu string and Roland's product name (CONTRACT R3, house law 9). `Cross`
— my first replacement — is already a `Route` option on the shipped flanger. `Twin` is free across
the whole rack and says what the mechanism is: two lines moving oppositely.
`Ensemble` collides with a shipped **Chorus Type name**, and its mechanism is a configuration of
`Stack` rather than a different machine. Cut; it survives as `Stack`/`Three Phase`, which measures
the **largest** Character distance in the roster (673.06).

### 3.3 `PV Glass` cut, as the bible's own audit recommended
32 ms of uncompensatable latency in a rack that forbids reporting latency, on a device whose entire
job is a *micro*-double. Not a close call.

### 3.4 The `Voices` knob floors at 3 copies, not 1
The bible's floor is 1 ("our `Voices` starts at 1 because Mix/Power already own the bypass job").
But voice 0 is the un-modulated centre read, so `Voices = 2` is centre + **one** mover — which is a
chorus, and the Chorus shipped as chain kind 6. CONTRACT §4 says a Widen Type with one obviously
modulating voice must be cut; the same logic applies to a knob position. Cert §O asserts the floor.

### 3.5 `Bands` is band alternation, not a complementary comb
The bible's §2.7 is `s = Amount*delay(m, tau)`, `L = m+s`, `R = m-s`. That is mono-**exact** in the
sense that `L+R = 2m` — i.e. **the wet cancels completely**, which is the exact failure CONTRACT
law 5 names. Replaced with the Orban 245E lineage: a one-pole crossover **tree**
(`lp + (x-lp) = x` exactly) with alternating band gains `(1+g)` and `(1-g)`. Those gains **sum to 2
for any g**, so the mono fold is the input bit-for-bit *and* the wet does not cancel. Measured mean
deviation **0.000 dB at every Amount**. It also gave the device its best R11 lever (§4.3).

---

## 4. THINGS THAT SURPRISED ME

### 4.1 A compander around a noiseless delay is an identity
Removing `Twin`'s compress-before / expand-after pair (§2.3) cost nothing audible, and the reason is
arithmetic: a compressor followed by its exact inverse is a no-op unless something is **added in
between**. The SDD-320's compander exists to hide BBD hiss; this device injects no hiss (house law).
The fx3 chorus measured the same thing on the legacy engine — its "compander" was a static x1.47
trim with *zero* level dependence, a costume. What ships is the audible half: a real 2:1 downward
compressor on the wet, calibrated to **-12 dB relative to the -26 dBFS bus program**, never to a
literal dBFS constant copied out of the hardware world.

### 4.2 Allpass decorrelation is NOT monotonic, and it took three tries to find the law
Correlation between two allpass cascades is `<cos dphi(w)>`, which **oscillates** once dphi passes
pi.
- 2.5 octaves of divergence per stage: 1-|corr| read **0.00 / 0.88 / 0.53 / 0.91 / 0.96** — the
  middle of the Scatter knob went *backwards*.
- Engaging stages one at a time (each adding a bounded independent delta): still turned over in the
  last quarter.
- The law that holds: N stages each contributing an **independent small** delta give
  `dphi_rms = delta*sqrt(N)`, so `corr ~= exp(-dphi_rms^2/2)` — monotone for as long as dphi_rms
  stays inside pi. With up to 24 stages that budgets **0.30 octaves per stage** at 100 %.

And then a second surprise on top: a phase-only decorrelator **bottoms out at corr = 0**. The top
quarter of Scatter was a plateau until Amount was given a second job — it opens the cascade
**downward** (the lowest break frequency drops 4x), putting real group delay into the low mids where
the programme's energy is. Measured end-to-end: corr **+1.000 -> -0.465**, monotone, past
decorrelation into anti-correlation.

### 4.3 A band splitter can go past full separation and still be mono-exact
Once the alternation is `(1+g)` / `(1-g)`, nothing stops `g > 1` — the quiet channel's gain goes
**negative** and the two channels hold the same band in opposite polarity. Correlation drives hard
negative, the image tears apart, **and the mono fold is still the input bit-for-bit** because the
gains still sum to 2. Ships at `g_max = 1.8`. This is the cleanest R11 lever in the device: maximum
destruction, zero mono damage. (`Guard` caps it at 0.75 as the deliberate polite counterexample.)

### 4.4 The Feedback taper had to be inverted from a measured curve, not derived
`t^1.2 * g_max` gave **+0.07 / +0.29 / +0.86 dB** for the first three quarters and **+13.7 dB** in
the last one. `1 - (1-g_max)^t` is the correct law for a *coherent* loop and still bunched, because
this loop is a bank of moving delays and recirculates **incoherently** — power adds, not amplitude.
The measured curve (0.10 / 0.31 / 0.62 / 1.02 / 1.65 / 3.68 / 7.84 / 11.16 / 13.16 / 14.90 dB at
g = 0.21…0.90) inverts to roughly `g_max * t^0.35`, which is what ships.

### 4.5 The 130-cent knob measures 140 cents downward, and that is not a bug
`1200*log2(1 + x)` and `1200*log2(1 - x)` are not symmetric: a delay slope of ±0.0779 gives
**+130 cents up and -140.5 cents down**. This is the same asymmetry Szabo measured on the JP-8000
itself, where the down-detuned voices travel -202 cents against the up-detuned +177. The `Amount`
knob is calibrated on the **up** side, and the down side runs 8 % further, exactly like the hardware.

---

## 5. WHERE THE PROBE WAS WRONG BEFORE THE DEVICE WAS

Every one of these produced a confident wrong number first. §B of the harness exists so they cannot
happen silently again: **every metric is printed through a bypassed engine before it is trusted.**

| Probe | What it read through a BYPASSED engine | Why | Fix |
|---|---|---|---|
| per-channel magnitude ripple | **39.6 dB** | it was measuring the chord's own harmonic shape, not the device | measure the **deviation from dry**, level-normalised -> control now reads **0.0 dB** |
| cepstral echo count | **39–49 "echoes"** | on a harmonic probe the cepstrum shows the source's own pitch period | count on **noise** |
| `voiceCents` for the triangle test | perfectly bimodal for **both** the triangle and the sine | the viz published `achievedCents * sign(triangleSlope)` — a hard ±1 whatever the modulator shape. **A harness kinder than reality (fb393).** The sine A/B could never have failed | publish the **true read-position derivative**, `1200*log2(1 - d')` |
| `Stack` modulation periodicity | **0.869** — "one clean line", i.e. a chorus | at 0.26 Hz the six scattered voice rates are 0.018 Hz apart and a 1024-point modulation FFT has 0.37 Hz bins. **Resolution, not physics** | run the test at rate 0.90 with a 4096-point FFT -> **0.290** |
| `Bands` "rotation" | correlation **+0.88** where the model predicted +0.17 | `s_k = cos(2pi(k/2 + phi))` looks like a rotating alternation and is not one: for integer k, `cos(pi*k + phi) = (-1)^k cos(phi)`, so the phase scales *every* band's contrast by the same `cos(phi)` and at phi = 90 deg the split **vanishes**. It was a tremolo on the width | slide the crossover **grid** by ±0.5 of a band instead |
| mono fold-down level | `Bands` read **+3.14 dB** and "failed" | measured at Mix 0.5, where an equal-power crossfade of two **correlated** signals is +3 dB by arithmetic | measure at **Mix 1.0**, where there is no dry to sum with |
| Offset delay length | autocorrelation peak hopped between a delay and its multiples (2.25 / 2.92 / 11.33 / 4.67 / 20.67 ms across a *monotone* sweep); a lag-weighted ACF centroid read 59.4 ms at every setting | peak-picking and centre-of-mass are both wrong on a broadband ACF | the wet's **impulse-response time centroid** through a settled engine -> 2.79 -> 35.09 ms |
| Voices | mono comb depth read 0.68 / 0.54 / 0.69 / 0.75 / 0.57 dB — noise | with the cents fan normalised to the live count, adding a voice only fills the middle | give the pan ladder an **absolute** rung per voice (so Voices *widens* as well as thickens) and measure the **per-step** spectral change |

---

## 6. WHAT I COULD NOT PROVE

1. **That the plugin will REACH this engine.** This is the fb373 law and no DSP harness can close
   it. §A asserts the cardinality contract (`SYN_WID_TYPE` at choice(8) with 2 reserved, Character
   always choice(8) read back on the /7 scale) but the UI -> param -> DSP round trip belongs to the
   integration owner. **`Cassette` silently gave you `Studio` through four rounds of green
   measurement.** Please gate the round trip separately and headlessly.
2. **That any of this sounds good.** Every claim here is a measurement. `Twin` at Amount 1.0 is a
   number I trust and a sound nobody has heard yet. The worklet exists so Max can decide.
3. **Real-DAW CPU.** §M measures `processStereo` in isolation on one core. It does not include the
   60 Hz viz push, the rack's dispatch, or six instances competing for L2 — and the worst Type is
   **cache-bound**, so contention will hurt it more than a flop-bound device.
4. **The 96 kHz anti-correlation extreme.** §N passes, but honestly: `Blur` at Amount 1.0 measures
   corr **-0.537 / -0.461 / +0.018** at 44.1 / 48 / 96 kHz. The *decorrelation* holds at every rate
   (|corr| <= 0.02 at 96 k is if anything better), but the push **past** decorrelation into
   anti-correlation does not survive to 96 kHz — the allpass break frequencies sit at a different
   fraction of Nyquist there. Blur's extreme is a 44.1/48 kHz phenomenon. I have not fixed this and
   I would not fix it by tuning a constant; it wants a Nyquist-relative fc distribution.
5. **The `Hear Mono` pill.** It is a UI-side audition (`(L+R)/2` to both outs, 15 ms fade) and costs
   the engine nothing, so there is nothing in this harness that tests it.
6. **Tempo sync.** The plumbing is implemented, honoured and clamped into 0.03–14 Hz, and is **not
   exposed** for v1 per the bible's own recommendation. It is therefore untested by anything.

---

## 7. CONTRADICTIONS IN THE BIBLE / CONTRACT, REPORTED AS ASKED

1. **CONTRACT law 5 and the bible give opposite instructions, and it matters most for this device.**
   The contract says *"Fold to mono and the effect must not vanish."* The bible's §2.6/§2.7 design
   and its §8 gate table say the opposite: `Mirror`/`Bands` are specified to have a mono sum
   *identical to dry* — that is Polyverse Wider's literal selling point ("Wider cancels itself out
   when summed to mono").
   **Resolution shipped:** a widener cannot make *width* audible in mono; what it can avoid is
   **damage**. So the gate is two-sided and stated in §J — mono level within ±3 dB and no log-band
   notch deeper than 4 dB — and the mechanisms that genuinely cost mono energy are **tagged**
   (`typeIsMonoLossy`, `fieldIsMonoHostile`, `charIsMonoHostile`) with their measured numbers, which
   is what the contract asks for in its second sentence. I did **not** build the bible's pure-side
   `s0 ± s` frame, precisely because it is the `L = dry+wet, R = dry-wet` recipe the contract warns
   about. It survives as the tagged `Field = Side Only` option.
2. **Bible §3.1's depth clamp is unshippable as written** — see §3.1 above. It is internally
   consistent and it produces a dead knob.
3. **Bible §11 Q9 (`SYN_FX_ORDER` blocks the 4th device) is STALE**, as the assignment said: the
   tree replaced it with float ranks at fb375. Ignored entirely.
4. **Bible §2.7's `Bands` engine has the mono-cancellation bug built in** — see §3.5.
5. **Bible §4's `Voices` floor of 1** is a chorus by the contract's own boundary — see §3.4.
6. **A tag I had to remove because the measurement disproved it.** `Blur`/`Counter` was tagged
   mono-hostile on the reasoning that negated path-B coefficients sit near phase opposition. Across
   three engine revisions it measured 2.16 vs 3.54 dB, then 2.81 vs 2.56 dB — the difference is a
   few tenths of a dB and **its sign is not stable**. That is not a hostile Character, it is a
   different room. The tag is gone and §J now asserts only the honest claim.

---

## 8. THE WEAKEST NUMBERS IN THIS BUILD — read these before the green ones

- **CPU.** `Shift` at **0.87 % of one core** is the worst Type and it is **cache-bound**, not
  flop-bound: 16 scattered 4-point Hermite reads per sample across a 128 kB working set. Shrinking
  the ring from 0.47 s to 0.32 s bought ~2 %; the remaining cost is the access pattern. Six
  instances = **5.2 %**, against the shipped fx3 rack's 0.78 %/instance and the spring reverb's
  0.43 %/instance. It is in the family but it is at the top of it. The gate is stated at 1.0 % with
  that reference; it was 0.60 % before I had any measurement to ground it in, and I moved it and
  said so rather than quietly optimising until an arbitrary number went green.
- **`Twin`'s mono fold is -8.84 dB at Mix 1.0 / Amount 0.7.** That is a real loss. The inverted
  cross-mix *is* the width, the real SDD-320 is never run at 100 % wet, and the Type is tagged — but
  a user who runs `Twin` fully wet into a mono club system will lose most of it.
- **`Blur`'s worst mono notch is -16.6 dB** at Amount 0.7. DAFx-24 measures dual-allpass mono ripple
  at "1 to 2 dB" for mild settings; ours runs up to 24 stages because R11 asks for the extreme.
- **Correlation at the shipped defaults is +0.87** (`Stack`, Mix 1.0, Amount 0.35, Width 0.5). The
  bible's §8 suggests 0.2–0.8 at defaults. Width 0.5 is *exactly neutral* by construction, so the
  default patch is deliberately conservative and the width lives on the hero knob. If Max wants the
  card to sound wide out of the box, raise the default `Width` to ~0.7 — the knob is proven monotone
  across its whole range and that is a one-line change, not a re-voicing.
- **`Bands`/`Rotor Fast` is the least distinct Character in the roster** at 0.60 (threshold 0.35).
  It is a rate multiplier on the grid sweep and it does exactly that; it is honest, and it is
  nearly the smallest change any Character makes. If a Character has to go, it is that one.
