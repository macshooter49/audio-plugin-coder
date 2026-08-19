# WIDEN — FINDINGS

**fb423 — THE NAMES ROUND.** Harness: `widen_cert.cpp` -> **230 PASS / 5 FAIL**
(`widen_cert_fb423.log`). Mutation proofs: **`MUTATION.md`**, now 11 mutations. The fb420 and
fb422 logs are kept beside this file for the diff.

🔴 **THE ONE THING I DID NOT DO LAST ROUND: I BUILT NO NO-DOUBLES GATE.** FIXES §2 said "rebuild
the no-doubles gate"; what §9 below actually was is a one-off markdown grep over the **15 strings
RENAMES.md had just changed**. My other **80 published labels had never been checked against
anything**, and the family audit found **12 collisions in this device that I did not**. That is
round one's failure repeating verbatim — *checking what I changed instead of the whole card.* §10
is the gate, §S of the cert is where it lives, and it covers **all 103** labels the engine
publishes. The old §9 is left in place, unedited, as the record of what a check that only looks at
its own diff is worth.

```
clang++ -O2 -std=c++17 \
  -I plugins/TerrainInstrument/Tests/shim \
  -I plugins/TerrainInstrument/Source \
  -I plugins/TerrainInstrument/Design/fx4/widen \
  plugins/TerrainInstrument/Design/fx4/widen/widen_cert.cpp -o /tmp/widen_cert && /tmp/widen_cert
```

---

## 0. 🔴 THE FIVE REDS THAT ARE STILL RED, AND WHAT THEY WOULD TAKE

Read these first. They are understood, not bought.

| # | gate | measured | why it is red | what it would take |
|---|---|---|---|---|
| 1 | `Rate` on **`Blur`** — alive and monotonic | quarters **2.13 / 3.27 / 3.68 / 21.1**, bar **5.86** | The knob is genuinely wired and its TOTAL travel (40.9) is 1.4× a whole quarter-turn of `Mix`. But `Rate` is logarithmic, so the bottom quarter moves the allpass field from 0.20 to 0.73 Hz and that is a small change against Blur's own huge dry→wet distance (its `Mix` reference is the largest of the six, 29.3). It fails **my own audibility anchor**, not a number I picked to suit it. | Either a modulation depth that grows toward the bottom of `Rate` (which couples two controls), or a shallower `Rate` law on this Type alone (which breaks the shared `Rate` grammar of CONTRACT §4). I would not do either without asking. |
| 2 | `Roam` on **`Blur`** — alive and monotonic | quarters **3.37 / 3.79 / 5.99 / 6.13**, bar **5.86** | Same shape, same cause: two of four quarters clear the anchor and two do not. | Same as above. |
| 3 | ALL 72 CELLS ARE ALIVE | **2 dead** (both of the above) | It is the roll-up of 1 and 2. | — |
| 4 | `Steady` — Feedback+Voices 100 % is a wall | **+2.89 dB**, bar **+3.0** | The granular reader **re-pitches the recirculated signal on every pass**, so each pass walks away in frequency from the last and the returns add in POWER, not amplitude. Three separate structural fixes (the anchor out of the loop, the compander out of the loop, 14 kHz damping) took it from +1.8 to +2.89 and it is still 0.11 dB short. | A multi-tap feedback return on the voice family (the two-tap trick that took `Blur` from +4.3 to +19.0), or a Type-specific loop that bypasses the granular reader. Both are new mechanisms and I did not invent one at 3 a.m. |
| 5 | `Twofold` — Feedback+Voices 100 % is a wall | **+2.03 dB**, bar **+3.0** | Same class: the random walk decorrelates each pass from the last. | Same. |

**None of these is a false green, and none of them is a gate I weakened.** Where I did move a
threshold I said so in the cert source, at the line, with the measurement that motivated it.

---

## 0b. WHAT THE ADVERSARIAL PASS FOUND, AND WHAT I DID ABOUT IT

| blocker | status | evidence |
|---|---|---|
| **R11 measured a trigonometric identity** | FIXED | §R is re-derived on `Amount` at 100 % per Type with **Width pinned at 0.5 (exact unity)**, plus `Feedback`+`Voices` at 100 % together. New: an **anti-Haas clause** — the six readings must differ, because "identical to three decimals" was the refutation's own signature. `MUTATION.md` §1 |
| **night-and-day gates read write-only telemetry** | FIXED | Nothing in §C/§F/§R reads `viz()` or `liveTargetCents()` any more. Pitch comes from the OUTPUT magnitude spectrum (`detuneSpreadCents`, `carrierMass`), motion from a short-time trace of the OUTPUT's own centroid / energy / correlation. §E still reads the solver and **now says so in its own header**. `MUTATION.md` §1 |
| **`Rate` bit-identically dead on Steady + Blur; `Spread` dead on Twin/Blur/Bands; `Roam` dead on Twin/Bands; `Balance` dead on Twin** | FIXED | Every one now has a real mechanism (ROSTER §3–4). The cert sweeps **12 knobs × 6 Types = 72 cells** and gates all 72. `MUTATION.md` §2 shows the fb421 behaviour reading **exactly 0.000** in nine of them |
| **the `Voices` floor does not apply to Twin; the chorus gate ran on the wrong Types** | FIXED | §O runs on **all six**, uses `liveCopies()` (the real per-machine count) and cross-checks it on the OUTPUT. The Twin Character names are now the counts: `Two Line` 2, `Four Line` 4, `Hex` 6. `MUTATION.md` §7 |
| **R11 politeness** | FIXED | `Twin` 28 → **163.9 measured cents**; `Spread` correlation travel 0.125 → **0.947 / 1.891 / 1.823 / 1.120** by Type. `MUTATION.md` §3 |
| **Blur's anti-correlation does not survive 96 kHz** | FIXED, and the cause was NOT where the old FINDINGS guessed | The ±0.97 clamp is a **coefficient** limit, hence a **sample-rate-dependent frequency** limit: 214 Hz at 44.1, 232 at 48, **465 at 96**. Now −0.866 / −0.867 / −0.871. `MUTATION.md` §8 |

---

## 1. THE HEADLINE MEASUREMENTS

| Claim | Number | Where |
|---|---|---|
| **The Detune knob reads TRUE CENTS, independent of Rate** — the gap the bible identified in Serum | peak cents **140.5 … 140.8** across a **12x** rate span (0.139 -> 5.569 Hz): **0.17 % spread** | §E |
| **The Dimension triangle holds its detune** | **99.2 %** of samples at \|c\| > 0.8·peak; **0.08 %** in the ±10 % dead zone | §D |
| **…and the A/B proves the detector can see the alternative** | the sine (`Tremble`) reads **0.435** lobe mass and **0.0627** zero dwell against closed-form arcsine predictions of **0.410** and **0.064** | §D |
| **Width goes past mono-destruction, with substance** | every Type: corr **-1.000**, mono fold **-128 to -144 dB**, output still within **-8.4 … +2.7 dB** of the Width-50 % level | §R |
| **`Bands` mono fold is spectrally EXACT** | mean deviation **0.000 dB at every Amount**, including past g = 1 where the quiet channel's band gain goes negative | §J |
| **Mix 1.0 = fully wet, zero dry** | dry residual **-142.1 to -142.4 dB**, with a **-3.01 dB** control at Mix 0.5 proving the probe can see dry at all | §H |
| **Feedback is a wall** | **+14.9 dB** of sustained density, taper calibrated so every quarter of the knob moves | §R, §F |
| **Every Type pair is distinct** | perceptual L2 >= **2.43** (1.0 = one audible step); closest pair still **3.1x** apart on its stated discriminator | §C |
| **Every one of the 48 Characters changes physics** | 0 weak cells; weakest **0.60** against a 0.35 threshold | §G |
| **60 s of full-drive white noise, every Type** | no NaN, peak <= **1.63** | §K |
| CPU, worst Type | `Steady` **23.2 us/block** = **0.87 %** of one core; x6 = 5.2 % | §M |

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
   (pure side — tiny on a near-mono wet) for `Gather` (pure mid — large): an **11x level change**,
   measured. 5 % of an 11x change is still a step. Floor moved to **0.005 (-46 dB)**.
2. The residue was worse and stranger: a Type swap produced a **3.17e-02** transient (250x the
   floor) *five blocks after* the swap, when the dip had already recovered to 0.28. Bisecting by
   Character found it: the `No Compander` Character dropped it to 1.02e-03. **`Twin` was writing
   companded, pre-emphasised audio into the SHARED ring buffer.** So the buffer's *content* stepped
   at the swap sample, and that step emerged one delay-time later — after any dip has recovered.
   No dip length fixes a discontinuity in stored signal.
   **Fix, structural:** the compander moved to the wet OUTPUT stage; the ring is now
   Type-independent, always. See §4.1 for why nothing was lost.

### 2.4 `Octave Bloom`, `Half Time` and a second Twofold variant were dead Characters
- `Octave Bloom` addressed voices `v >= 6`. The **default voice count is 6**, so it addressed
  nothing. Distance measured **exactly 0.00**. Now it addresses the top two **live** voices.
- `Half Time` set `baseMul = 0.5`, but the base is depth-driven (§3.1) so the growth floor swallowed
  it entirely — **0.00**. Re-voiced as `Mode Two` = a 2.2x clock, which under the constant-cents law
  *is* the shorter-delay mode Arturia measured.
- The second Twofold variant's coefficient row was **character-for-character identical** to char 0's — a
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
`Throng` rather than a different machine. Cut; it survives as `Throng`/`Three Phase`, which measures
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
  middle of the Wash knob went *backwards*.
- Engaging stages one at a time (each adding a bounded independent delta): still turned over in the
  last quarter.
- The law that holds: N stages each contributing an **independent small** delta give
  `dphi_rms = delta*sqrt(N)`, so `corr ~= exp(-dphi_rms^2/2)` — monotone for as long as dphi_rms
  stays inside pi. With up to 24 stages that budgets **0.30 octaves per stage** at 100 %.

And then a second surprise on top: a phase-only decorrelator **bottoms out at corr = 0**. The top
quarter of Wash was a plateau until Amount was given a second job — it opens the cascade
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
| `Throng` modulation periodicity | **0.869** — "one clean line", i.e. a chorus | at 0.26 Hz the six scattered voice rates are 0.018 Hz apart and a 1024-point modulation FFT has 0.37 Hz bins. **Resolution, not physics** | run the test at rate 0.90 with a 4096-point FFT -> **0.290** |
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
4. ~~**The 96 kHz anti-correlation extreme.**~~ 🔴 **fb422: SOLVED, and the fb421 diagnosis above
   was wrong.** It is not "the break frequencies sit at a different fraction of Nyquist" — the
   break frequencies are set by a bilinear `tan(pi*f/fs)` and are sample-rate correct. The cause is
   the **clamp**: `apCoef` limited the COEFFICIENT to +-0.97, and the frequency that produces
   |c| = 0.97 is `fs * atan(0.01523) / pi` = **0.485 % of fs** — 214 Hz at 44.1 kHz, 232 at 48 and
   **465 at 96**. So the low stages, which are exactly the ones `Wash` opens downward to keep the
   top of the knob alive, were silently dragged to a DIFFERENT frequency at every sample rate, and
   at 96 kHz half the cascade collapsed onto the probe's own fundamental range. The clamp now sits
   on the FREQUENCY (8 Hz) with a pure numerical guard at 0.99975 that is below it at every rate.
   Measured after: **-0.866 / -0.867 / -0.871**. `WIDEN_MUT_APCLAMP` restores the old clamp and
   reproduces **-0.345 / -0.299 / +0.243** on demand.
   🔑 And **the old §N gate would not have caught it even now**: it asserted `corr < 0.30 at every
   rate`, and `+0.243 < 0.30` passes. The gate now asserts the claim the section is actually
   making — that the device is the SAME at every sample rate, all three within 0.10 — and it fires.
5. **The `Hear Mono` pill.** It is a UI-side audition (`(L+R)/2` to both outs, 15 ms fade) and costs
   the engine nothing, so there is nothing in this harness that tests it.
6. **Tempo sync.** The plumbing is implemented, honoured and clamped into 0.08–14 Hz, and is **not
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
6. **A tag I had to remove because the measurement disproved it.** `Blur`/`Opposed` was tagged
   mono-hostile on the reasoning that negated path-B coefficients sit near phase opposition. Across
   three engine revisions it measured 2.16 vs 3.54 dB, then 2.81 vs 2.56 dB — the difference is a
   few tenths of a dB and **its sign is not stable**. That is not a hostile Character, it is a
   different room. The tag is gone and §J now asserts only the honest claim.

---

## 8. THE WEAKEST NUMBERS IN THIS BUILD — read these before the green ones

- **CPU.** `Steady` at **0.87 % of one core** is the worst Type and it is **cache-bound**, not
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
- **Correlation at the shipped defaults is +0.87** (`Throng`, Mix 1.0, Amount 0.35, Width 0.5). The
  bible's §8 suggests 0.2–0.8 at defaults. Width 0.5 is *exactly neutral* by construction, so the
  default patch is deliberately conservative and the width lives on the hero knob. If Max wants the
  card to sound wide out of the box, raise the default `Width` to ~0.7 — the knob is proven monotone
  across its whole range and that is a one-line change, not a re-voicing.
- **`Bands`/`Rotor Fast` is the least distinct Character in the roster** at 0.60 (threshold 0.35).
  It is a rate multiplier on the grid sweep and it does exactly that; it is honest, and it is
  nearly the smallest change any Character makes. If a Character has to go, it is that one.

---

## 9. 🔴 fb422 — THE NO-DOUBLES CHECK, RUN THE WAY RENAMES.md ASKS

All 15 WIDEN rows of `Design/fx4/RENAMES.md` applied **verbatim**, then every new name grepped
against `Source/` (including `Source/ui/public/index.html`, which the fb421 check skipped),
`Design/fx3/` and **both sibling fx4 directories**:

```
NAME         | Source/ (quoted string) | fx3 + eq/ + dynamics/
Steady       |   0 (+0 html)           |   1     <- prose reference in dynamics/FINDINGS.md, not a label
Twofold      |   0 (+0 html)           |   1     <- same line
Roam         |   0 (+0 html)           |   0
Wash         |   0 (+0 html)           |   0
Sway         |   0 (+0 html)           |   0
Tight Fan    |   0 (+0 html)           |   0
Two Line     |   0 (+0 html)           |   0
Four Line    |   0 (+0 html)           |   0
Satin        |   0 (+0 html)           |   0
Jab          |   0 (+0 html)           |   0
Octave Down  |   0 (+0 html)           |   0
Static Pair  |   0 (+0 html)           |   0
Opposed      |   0 (+0 html)           |   0
Top Only     |   0 (+0 html)           |   0
Deep Grid    |   0 (+0 html)           |   1     <- dynamics_cert.cpp comment citing this very table
```

The three non-zero hits are all **the sibling agents writing ABOUT these names in prose**, not
using them as labels. Zero real collisions.

### and the labels now come from the engine (FIXES §3)

`TerrainWidenFx.h` publishes **every string the card can print**:

```cpp
static const char* deviceName();                 // "Widen"
static const char* const* typeNames();           // 6 header pills
static const char* const* charNames (int type);  // 8 per Type
static const char* const* fieldNames();          // the 6 Field options (back dropdown 2)
static const char* const* frontNames (int type); // hero1 (RELABELLED PER TYPE) + Width + Rate + Mix
static const char* const* backNames();           // the 8 back knobs
static const char* const* pillNames();           // Retrig · Hear Mono
static const char* voicesUnit (int type);        // "copies" | "lines" | "stages" | "bands"
static const char* const* divNames();            // the 20 sync divisions
```

At fb421 only `typeNames()` and `charNames()` existed; the per-Type `Amount` relabel and all eight
back-knob names lived **only in ROSTER.md**. `widen-worklet.js` now carries a header saying it
MIRRORS these and that the header wins on any disagreement.

### and the `Voices` unit is published too, because it is not "copies" on three of six Types

`Twin` counts **lines**, `Blur` counts **stages**, `Bands` counts **bands**. `liveVoices()` returns
`nV_` and is therefore the wrong number on three Types; `liveCopies()` returns the real one and §O
cross-checks it against the OUTPUT.

---

## 10. 🔴 fb423 — THE GATE I SHOULD HAVE BUILT IN ROUND TWO

**Cert §S. 103 published labels, every one of them, on every run.** Not the ones I changed.

### What it checks, and what each check caught

| gate | result |
|---|---|
| the card label set is 6 Types + 48 Characters + 6 Fields + 6×4 front + 8 back + 2 pills + 1 device | **95** card labels; **103** gated in total (+2 back-dropdown labels, +6 `Voices` units) |
| no published label collides with a shipped or sibling label | **0** unruled hits against a **3069-string** corpus (re-extracted after the siblings' latest renames landed) |
| corpus sees the fb418 LEADING-SPACE labels | `Motion` PRESENT · `Route` PRESENT |
| corpus sees SINGLE-quoted `index.html` options | `Leaky` · `Metal` — both single-quoted, both invisible to a C++-literal grep |
| corpus sees the shipped Tape front knobs the first EQ table missed | `Tilt` · `Sculpt` |
| corpus sees BOTH sibling fx4 directories | `Slant` · `Chisel` (eq) · `Free Pair` · `Crest` (dynamics) |
| no word names two DIFFERENT controls inside this card | **86** distinct control labels, all unique |
| RENAMES.md parses to 23 WIDEN rename rows | 15 first table + 8 fb423 |
| every NEW name in RENAMES.md is PUBLISHED by the engine | **all 23 present** — the table was applied, and the gate proves it rather than my saying so |
| `widen-worklet.js` name tables EQUAL the header arrays | **7 tables, 100 entries, identical** |
| ROSTER.md carries every published card label as a backticked literal | **96 labels, all present** |
| no RETIRED label survives as a label token in header/worklet/roster | 23 retired names × 3 artefacts, **0 hits** |
| ...nor in this harness's own printed output | **0** |

**The corpus.** `extract_labels.py` → `shipped_labels.inc`: **3069 strings from 166 files** across
`Source/` (including `ui/public/index.html`'s option arrays and every `*_test.cpp` name table),
`Tests/`, `Design/fx3/` and **both** sibling fx4 directories. Three blindnesses fixed, each of
which had hidden a real collision from somebody:

1. **leading spaces** — `"Chorus" + sfxD + " Motion"` means the literal is `" Motion"`, and a
   "quote followed by a capital" pattern skips both strings R6 is named after. Literals are
   stripped *before* the capitalisation test.
2. **single quotes** — `index.html` is JS. The shipped Distortion character `'Leaky'` hid there.
3. **the corpus itself** — a C++-literal grep over `Source/` reaches neither of the above, nor the
   `*_test.cpp` tables, nor the siblings.

### What it found in MY device that the previous pass could not see

Run against the fb422 engine, §S reports **36 collision hits over 101 labels**. Eight of them are
the fb423 rows, now applied: `Stack`→`Throng` (my **header pill**), `Split`→`Cleave` (the **Bands
front hero**), `Vocal`→`Lilt`, `Warble`→`Quaver`, `Velvet`→`Plush`, `Direct`→`Straight`,
`Collapse`→`Gather`, `Wobble`→`Tremble`.

### The 22 stale strings, in my own files

The drift gates found retired names still being **printed by my own harness** — a `Knob` table in
§F and a second `nm[12]` table in §I, one of which still said `P4 Wander` (retired by the first
RENAMES table) while the engine said `Roam`. *That is literally the two-table geometry the EQ
engine deleted, living in my cert.* **Both tables are deleted**, not gated: `knobLabel()` builds
every printed knob name from `frontNames()`/`backNames()` at run time. A table that cannot exist
cannot drift. The remaining stale tokens (`Duo`, `Quad`, `Opposed`'s predecessor) are gone from the
header, the worklet, the roster and the harness, and the gate keeps them gone.

### 🔴 THE FIVE EXEMPTIONS I GRANTED MYSELF, NAMED SO THEY CANNOT HIDE

The exemption lists are asserted to be **exactly** 10 / 2 / 5 entries, the dynamics §1 shape, so
none of them can quietly grow. Two carry an authority:

- **A (10)** `Width` `Rate` `Mix` `Amount` — CONTRACT §4 names these; `Character` `Type` `Power` —
  chassis; `Tone` `Retrig` — fb423 SANCTIONED verbatim; `Low Keep` — RENAMES.md "keep".
- **B (2)** `Blur` `Coarse` — fb423 SANCTIONED verbatim: *"mockup-only / synth-side strings, not
  live rack labels. No action."*

The third has none, and I am flagging it rather than burying it:

- **C (5) — UNRULED.** `Detune` `Depth` `Voices` `Spread` `Feedback` all hit the corpus. **No
  ruling covers them**: neither RENAMES table names them and CONTRACT §4's enumerated list does not
  include them. I believe every one is the §4 class — same concept, same behaviour, different
  device (`Detune` = unison detune, `Depth` = modulation excursion, `Voices` = how many copies,
  `Spread` = fanning copies across the field, `Feedback` = regeneration). **But a gate that can
  exempt itself is not a gate** — fb423 said exactly that about the dynamics `Auto`. They are
  printed by name on every cert run and they need a ruling. If it goes the other way, five renames
  land here and none of them is hard.

### 🔴 And `Hear Mono` was a pill with no parameter

ROSTER §3 described it as a "UI-side audition… costs the engine nothing" — i.e. a control that
cannot be automated, cannot be saved and cannot be recalled. It is `Params::hearMono` now (cert
§Q): ON gives max |L−R| = **0.000000e+00**, the output IS the mid of the OFF run to **2.4e-06**,
`viz().corr` reads **+1.00000** (the fold is measured *before* the telemetry, so the card shows
what you hear), and toggling it under a held tone leaves the click metric at **0.0026 → 0.0026**.

One real bug fell out of building it, and it is worth stating because it will recur: **a one-pole
fade does not reach its target in float — it STALLS.** Near 1.0 the representable spacing is
5.96e-8, so once `k·(1−x)` drops below half of that the increment rounds away entirely. Measured:
`monoSm_` parked at **0.999978542** forever, leaving **1.48e-06** of L−R in a signal that is
supposed to *be* mono. The gate that asserts `max |L−R| == 0.0` is what caught it; a "< 1e-4" gate
would have passed and shipped a mono button that does not produce mono.
