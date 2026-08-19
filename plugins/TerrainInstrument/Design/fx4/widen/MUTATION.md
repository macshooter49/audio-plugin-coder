# WIDEN — MUTATION.md (fb423)

**FIXES.md §0.** Every law-1 (night-and-day), law-4 (no clicks) and R11 (ceiling) gate in
`widen_cert.cpp` is proven here to go **RED** against a deliberately broken copy of the engine.
A gate that survives its own mutation is a BLOCKER, not a footnote.

The mutations live in `TerrainWidenFx.h` behind `#ifdef WIDEN_MUT_*`. **The shipping build defines
none of them** — §Z asserts that and prints `MUTATION: NONE (shipping build)` in the banner.

```
# shipping
clang++ -O2 -std=c++17 -I <TI>/Tests/shim -I <TI>/Source -I <TI>/Design/fx4/widen \
  widen_cert.cpp -o /tmp/widen_cert && /tmp/widen_cert
# any mutation
clang++ -O2 -std=c++17 -DWIDEN_MUT_HAAS  ...same...  && /tmp/widen_cert_haas
```

**Shipping baseline: `PASS 230  FAIL 5`.** Every log in this table is on disk beside this file
as `widen_mut_<MACRO>.log`, produced by the same `widen_cert.cpp` as `widen_cert_fb423.log`. The
five are named and explained in FINDINGS §0 — they are real, understood, and not bought.

🔴 **fb423 adds three mutations, and two of them protect things that are not DSP.** A name gate and
a monitor pill are exactly the kind of thing that gets asserted rather than measured, so they are
mutated like everything else. **No pre-existing mutation regressed**: HAAS 78, DEADKNOBS 14,
POLITE 14, NOSMOOTH 13, NOGLIDE 11, NODIP 9, NOFLOOR 21, APCLAMP 9 — identical fail counts to
fb422, on 24 more gates.

---

## The table

| # | mechanism deleted | macro | result | the gates that fired |
|---|---|---|---|---|
| 1 | the ENTIRE widening machine → a fixed 12 ms Haas delay | `WIDEN_MUT_HAAS` | **157 / 78** | 78 gates, incl. all 6 R11 Amount, all 6 Type discriminators, 40 of the 72 matrix cells, the Dimension tell, the cross-type matrix, the bloom-by-Type clause |
| 2 | fb421's dead knobs restored | `WIDEN_MUT_DEADKNOBS` | **221 / 14** | 9 matrix cells at **exactly 0.000** change per quarter |
| 3 | fb421's ceilings restored | `WIDEN_MUT_POLITE` | **221 / 14** | R11 Twin + Twofold, 5 matrix cells, the chorus boundary on Twin |
| 4 | every smoother → tau 0 | `WIDEN_MUT_NOSMOOTH` | **222 / 13** | 7 click gates, up to **×488** of the static floor |
| 5 | delay/gain/pan glide removed | `WIDEN_MUT_NOGLIDE` | **224 / 11** | 5 click gates, up to **×488** |
| 6 | the fade-swap-recover dip removed | `WIDEN_MUT_NODIP` | **226 / 9** | all 3 discrete-switch gates, **+38 dB** worse than the bar |
| 7 | the Voices floor of 3 removed | `WIDEN_MUT_NOFLOOR` | **214 / 21** | the label-vs-count gates and 3 of the 6 output-side chorus gates |
| 8 | the fb421 ±0.97 allpass clamp restored | `WIDEN_MUT_APCLAMP` | **226 / 9** | the sample-rate invariance gate at 96 kHz |
| 9 | one fb423 rename reverted **in the header only** | `WIDEN_MUT_STALENAME` | **226 / 9** | 4 gates in §S: the shipped-corpus gate, the RENAMES-was-applied gate, and **both** drift gates |
| 10 | the `Hear Mono` fold deleted | `WIDEN_MUT_NOMONO` | **227 / 8** | all 3 §Q measurement gates |
| 11 | the `Hear Mono` 15 ms fade → a hard cut | `WIDEN_MUT_MONOSNAP` | **229 / 6** | the §Q click gate, at **×147** the static floor |

---

## 1 · `WIDEN_MUT_HAAS` — the skeptic's own refutation, verbatim

`procVoices`, `procTwin`, `procBlur`, `procBands` — the crowd, the detune, the motion, every
per-Type mechanism — replaced by `wetR = readH(bufR_, 0.012*fs)`, identical on all six Types.

**This is the mutation that passed fb421's R11 with BETTER numbers, identical to three decimals.**

| gate | shipping | Haas | verdict |
|---|---|---|---|
| Throng — Amount 100 % is past useful (R11) | **106.55** cents (control 1.69) | **1.69** cents | RED |
| Twin — Amount 100 % is past useful (R11) | **163.91** cents | **1.69** cents | RED |
| Steady — Amount 100 % is past useful (R11) | **144.29** cents | **1.69** cents | RED |
| Twofold — Amount 100 % is past useful (R11) | **55.84** cents | **1.69** cents | RED |
| Blur — Amount 100 % is past useful (R11) | corr **−0.87** | corr **+1.00** | RED |
| Bands — Amount 100 % is past useful (R11) | corr **−0.55** | corr **+1.00** | RED |
| R11 can tell the six Types apart | spread **69.1 %** | spread **0.0 %** | RED |
| every Type pair ≥ 1.0 audible step | closest pair **2.59** | closest pair **0.00** | RED |
| Throng — the detune IS the motion | spread 106 c, motion 0.42 of it | spread **2 c**, motion **0.00** | RED |
| Twin — a wide field with a constant detune | carrier mass **0.012** | carrier mass **1.000** | RED |
| triangle detune is BIMODAL | 0.99 of samples at ±peak | **0.000** | RED |
| Amount on Throng — alive and monotonic | 1.69 → **106.5** cents | 1.69 → **1.69** | RED |
| the bloom differs BY TYPE | **0** Type pairs tied | **7** Type pairs tied (+6.71 dB on four Types, to 2 dp) | RED |

Pasted, from `/tmp/MUT_HAAS.log`:

```
        Throng    detune spread, cents       1.691   control     1.691   bar   60.00    <<< FAILS
        Twin     detune spread, cents       1.691   control     1.691   bar   60.00    <<< FAILS
        Steady   detune spread, cents       1.691   control     1.691   bar   60.00    <<< FAILS
        Twofold  detune spread, cents       1.691   control     1.691   bar   45.00    <<< FAILS
        Blur     stereo correlation         1.000   control     1.000   bar   -0.25    <<< FAILS
        Bands    stereo correlation         1.000   control     1.000   bar   -0.15    <<< FAILS
          measured: Throng 1.7 · Twin 1.7 · Steady 1.7 · Twofold 1.7 cents  ->  spread 0.0 %
  FAIL  R11 can tell the six Types apart (it is not reading an identity) pitch-Type spread 0.0 % of the largest
  FAIL  the bloom differs BY TYPE (a shared machine would bloom identically) 7 Type pairs within 0.25 dB
```

> 🔑 **The mutation wrote a gate.** The `Feedback+Voices` block stayed GREEN under Haas and
> printed **+6.71 dB on four Types to two decimals** — the loop survives the gutting, and with one
> machine instead of six it blooms identically on all of them. That is the same signature as the
> original refutation, so "identical across Types" is now itself a failure condition, in both
> R11-B and R11-C. It did not exist before this run.

---

## 2 · `WIDEN_MUT_DEADKNOBS` — fb421's dead knobs restored

`Rate` ignored on Steady + Blur · `Spread` ignored on Twin + Blur + Bands · `Roam` ignored on
Twin + Bands · `Balance` ignored on Twin.

| matrix cell | shipping (change per quarter) | fb421 | verdict |
|---|---|---|---|
| Rate on **Steady** | 12.4 / 12.6 / 24.2 / 30.1 | **0.000 / 0.000 / 0.000 / 0.000** | RED |
| Rate on **Blur** | 2.0 / 3.3 / 3.7 / 21.1 | **0.000 ×4** | RED |
| Spread on **Twin** | 1.366 → 1.891 | **1.316 → 1.316** (0.000 ×4) | RED |
| Spread on **Blur** | 1.280 → 1.823 | **1.828 → 1.828** | RED |
| Spread on **Bands** | 0.016 → 1.120 | **1.171 → 1.171** | RED |
| Roam on **Twin** | 10.6 / 7.1 / 11.6 / 8.4 | **0.000 ×4** | RED |
| Roam on **Blur** | 0.5 / 0.6 / 1.1 / 1.1 | **0.000 ×4** | RED |
| Roam on **Bands** | 2.4 / 5.0 / 2.1 / 5.4 | **0.000 ×4** | RED |
| Balance on **Twin** | −8.08 → **+14.59** dB | **2.845 → 2.845** dB | RED |
| ALL 72 CELLS ARE ALIVE | 2 dead | **9 dead** | RED |

```
   x  Rate on Steady — alive and monotonic  [0.000 -> 0.000, smallest quarter 0.000 (>= 3.729)]
   x  P2 Spread on Twin — alive and monotonic  [1.316 -> 1.316, smallest quarter 0.000 (>= 0.030)]
   x  P4 Roam on Bands — alive and monotonic  [0.000 -> 0.000, smallest quarter 0.000 (>= 3.018)]
   x  P8 Balance on Twin — alive and monotonic  [2.845 -> 2.845, smallest quarter 0.000 (>= 0.500)]
   x  ALL 72 KNOB x TYPE CELLS ARE ALIVE (fb421 had 8 dead ones)  [9 dead cells]
```

> These are **exact zeros**, not small numbers. That is what "bit-identically dead" looks like
> when a gate can finally see it — and fb421's §F, which swept P1–P8 on `Throng` only, printed
> green for every one of them.

---

## 3 · `WIDEN_MUT_POLITE` — fb421's ceilings restored

Throng 130 / Twin **28** / Steady 110 / Twofold 62 cents; `Spread` reach 1.0× instead of 1.60×;
cross-mix `0.25 + 0.40·amt` instead of `0.25 + 0.70·amt`.

| gate | shipping | fb421 | verdict |
|---|---|---|---|
| **Twin** — Amount 100 % is past useful (R11) | **163.91** cents | **37.60** cents (bar 60) | RED |
| **Twofold** — Amount 100 % is past useful (R11) | **55.84** cents | **28.51** cents (bar 45) | RED |
| Amount on Throng — alive | 1.69 → 106.5, min quarter 7.6 | min quarter **5.08** (bar 6.0) | RED |
| Amount on Twin — alive | 1.69 → 147.3, min quarter 12.7 | min quarter **4.22** | RED |
| Twin — a wide field with a constant detune | spread 147 c, carrier 0.012 | spread **36** c, carrier 0.142 | RED |
| Twin — not a chorus | 2 copies, carrier **0.012** | 2 copies, carrier **0.993** | RED |
| the `parked` clause is falsifiable | triangle 0.012 vs sine 0.125 | triangle **0.993** vs sine 0.733 | RED |

> 🔑 At the fb421 ceiling the Dimension pair no longer vacates the carrier at all
> (0.993 of the spectrum still sits within ±25 cents of the probe tone) — so at fb421's own
> `Amount` maximum, `Twin` **was measurably a chorus**, and its own chorus gate never ran on it.

---

## 4 · `WIDEN_MUT_NOSMOOTH` — every smoother → tau 0

| click gate (bar = ×3 of the static control) | shipping | tau 0 |
|---|---|---|
| Amount sweep | **×1.07** | **×54.75** |
| Rate sweep | ×1.21 | **×48.19** |
| P1 Voices sweep | ×1.02 | **×488.76** |
| P3 Offset sweep | ×1.02 | **×106.91** |
| P4 Roam sweep | ×1.22 | **×11.94** |
| P6 Tone sweep | ×1.34 | **×4.92** |
| P8 Balance sweep | ×1.00 | **×6.58** |

```
   x  P1 Voices sweep is click-free  [peak d2 1.26e+00 vs static control 2.59e-03 (x488.76)]
```

## 5 · `WIDEN_MUT_NOGLIDE` — delay lengths / gains / pans snap

Same five of those go red (Amount ×54.75, Rate ×48.53, Voices ×488.76, Offset ×106.91,
Balance ×6.58) — i.e. **the comb-click law is carried by `glideGeom`, not by the parameter
smoothers**, and the two are now separately proven.

## 6 · `WIDEN_MUT_NODIP` — the fade-swap-recover dip removed

| gate (bar = −30 dB of programme peak) | shipping | no dip |
|---|---|---|
| Type swap | **−33.8 dB** | **+4.8 dB** |
| Character swap | −36.8 dB | **+4.4 dB** |
| Field swap | −35.5 dB | **+5.7 dB** |

38.6 dB worse than the shipping build and 34.8 dB over the bar.

## 7 · `WIDEN_MUT_NOFLOOR` — the Voices floor of 3 removed, Twin pinned to one pair

| gate | shipping | no floor |
|---|---|---|
| Throng/JP Classic — count matches the LABEL | **3** copies | **1** copy | RED |
| Throng — not a chorus (OUTPUT) | 3 copies, carrier 0.216 | **1 copy, carrier 1.000** | RED |
| Steady — not a chorus (OUTPUT) | 3 copies, carrier 0.046 | **1 copy, carrier 1.000** | RED |
| Twofold — not a chorus (OUTPUT) | 3 copies, carrier 0.852 | **1 copy, carrier 0.882** | RED |
| P1 Voices on Twin — alive | 12.8 / 9.7 / 6.7 | **0.000** (one pair at every setting) | RED |

> 🔑 fb421's §O measured `nV_` on `Throng` and `Twofold` only. Both of the Types the blocker named
> — `Twin` and `Steady` — are now in the table, the count is `liveCopies()` (the REAL count for
> the machine that is running: `2·nPair_` on Twin, `nAP_` on Blur, `nB_` on Bands), and it is
> cross-checked against the OUTPUT.

## 8 · `WIDEN_MUT_APCLAMP` — the fb421 ±0.97 allpass coefficient clamp restored

This is the fix for FINDINGS §6.4, and the mutation is the proof that the fix is the fix.

| | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| **shipping** Blur corr | **−0.866** | **−0.867** | **−0.871** |
| **fb421 clamp** Blur corr | −0.345 | −0.299 | **+0.243** |

```
  FAIL  Blur decorrelation is SAMPLE-RATE INVARIANT (44.1 / 48 / 96 within 0.10)
        corr -0.345 / -0.299 / +0.243
```

> 🔑 **And the OLD gate would not have caught it.** fb421 asserted `corr < 0.30 at every rate`,
> and `+0.243 < 0.30` — the mutated build **passes** the old wording. The gate now asserts what
> the section actually claims (the device is the SAME at every sample rate) and fires.

---

## 9 · `WIDEN_MUT_STALENAME` — the no-doubles gate, mutated

The header's `typeNames()` gets `Stack` back (a shipped FM algo + spectral mode) and `charNames()`
gets `Wobble` back (a shipped Tape preset). **The worklet and the roster are left correct**, which
is the point: this is what *drift* looks like, not what a typo looks like.

| gate | shipping | one rename reverted |
|---|---|---|
| no published label collides with a shipped or sibling label | 103 labels vs 3067 corpus strings, **0 hits** | **2 collisions, first `Stack` (Type pill)** |
| every NEW name in RENAMES.md is PUBLISHED by the engine | all 23 present | **2 missing, first `Throng`** |
| `widen-worklet.js` tables EQUAL the header arrays | 7 tables, 100 entries identical | **`TYPES[0]` worklet `Throng` vs header `Stack`** |
| ROSTER.md carries every published card label | 96 labels, all present | **2 missing, first `` `Stack` ``** |

The two mutated strings live between `MUT-STALENAME-BEGIN/END` markers so the *retired-label* scan
does not trip over the mutation itself; cert §S asserts there are **exactly two** such regions, so
the exclusion cannot grow into a hiding place.

---

## 10 · `WIDEN_MUT_NOMONO` — the `Hear Mono` fold deleted

| gate | shipping | fold deleted |
|---|---|---|
| ON: L and R are BIT-IDENTICAL | **0.000000e+00** | **2.202e-01** |
| ON: the output IS (L+R)/2 of the OFF run | 2.4e-06 | **1.10e-01** |
| ON: `viz().corr` follows to +1.000 | **+1.00000** | **+0.43521** |

The third row is the one that matters for the house law *everything audible interacts visually*:
with the fold gone the card still reads the true correlation of a wide signal, so a user pressing
`Hear Mono` would see nothing change either.

---

## 11 · `WIDEN_MUT_MONOSNAP` — the 15 ms fade replaced by a hard cut

| gate | shipping | hard cut |
|---|---|---|
| toggling `Hear Mono` under a held tone is click-free | **0.0026 → 0.0026** (no change) | **0.0026 → 0.3838**, ×147 |

The toggle is deliberately placed at `FS·0.5 + 37` samples — off a zero crossing *and* off a
block boundary. fb423's own ruling on the Compress device is why: its click probe jumped at
`FS·0.5` with a 220 Hz tone, which is 110 whole cycles **and** a 64-sample boundary, and a gain
step at a zero crossing produces no sample-to-sample jump at all.

---

## Gates that SURVIVE a mutation — declared, not hidden

| gate | survives | why that is acceptable |
|---|---|---|
| §E the constant-cents law | `HAAS` | It is a **structural check on the solver**, reads `liveTargetCents()`, and now says so in its own header. Its audible counterparts are §F and §R, which both go red under `HAAS`. |
| §K stability (60 s white noise) | all | It asserts finiteness. A Haas delay is finite; that is correct, not a false green. |
| §M CPU | all | Same. |
| §H Mix 1.0 dry residual | `HAAS` | Structural: the probe forces `wetL = +s, wetR = −s`, so it measures the MIX law, not the machine. |
| §S the whole names section | every DSP mutation | Correct, and worth saying: labels are not the machine. §S goes red only under `STALENAME`, which is the mutation built for it. |
| §Q `Hear Mono` | `HAAS` and every other DSP mutation | The fold is downstream of the widening machine, so it still folds a Haas delay to mono correctly. Its own mutations are `NOMONO` and `MONOSNAP`. |

No law-1, law-4 or R11 gate survives its own mutation.
