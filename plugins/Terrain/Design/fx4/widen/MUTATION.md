# WIDEN — MUTATION.md (fb425)

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

**Shipping baseline (fb425): `PASS 1511  FAIL 88` over 1599 gates.** Every log in this table is on
disk beside this file as `widen_mut_<MACRO>.log`, produced by the same `widen_cert.cpp` as
`widen_cert_fb425.log`, and **every one was re-run from scratch at fb425** — the counts below are
not fb423's carried forward.

🔴 **fb425 — THE FULL-MATRIX ROUND, AND EVERY OLD MUTANT GOT LOUDER.** Law-1, law-3 and R11 now run
the whole 6 × 8 Type × Character matrix (576 knob cells, 48 ceiling cells, 48 Mix cells), both front
pills are gated on all 48 cells, and the `Field` dropdown has a physics gate for the first time.
`WIDEN_MUT_HAAS` went from **78** fired gates to **513**. Two mutations are new, and each protects
one of the two things fb424 could not see at all:

| new macro | what it restores | what it proves |
|---|---|---|
| `WIDEN_MUT_RETRIGLFO` | fb424's `fireRetrig()`, which only reset the per-voice LFO phase | **23 cells go BIT-IDENTICAL** — 7 of 8 `Twofold`, 8 of 8 `Blur`, 8 of 8 `Bands`: exactly the three families that do not read `ph_[]` |
| `WIDEN_MUT_FLATFIELD` | `Swap` and `Gather` collapsed into `Straight` | **144 identical `Field` pairs** across all 48 cells. Under fb424's cert this mutation moved **zero** lines |

*(fb423's note, kept for the record: "fb423 adds three mutations, and two of them protect things
that are not DSP. A name gate and a monitor pill are exactly the kind of thing that gets asserted
rather than measured, so they are mutated like everything else. No pre-existing mutation regressed:
HAAS 78, DEADKNOBS 14, POLITE 14, NOSMOOTH 13, NOGLIDE 11, NODIP 9, NOFLOOR 21, APCLAMP 9 —
identical fail counts to fb422, on 24 more gates.")*

---

## The table

**Shipping: `1511 / 88`.** Every row below is worse than that, and the Δ column is what the
mutation cost — i.e. how many gates could see it.

| # | mechanism deleted | macro | fb425 result | Δ vs shipping | the gates that fired |
|---|---|---|---|---|---|
| 1 | the ENTIRE widening machine → a fixed 12 ms Haas delay | `WIDEN_MUT_HAAS` | **750 / 513** | **+425** | **336 of the 576 matrix cells go BIT-IDENTICAL** (the Haas delay has no knobs to be alive on), all 48 R11 `Amount` cells, all 6 Type discriminators, all 48 `Field` cells, the Dimension tell, the cross-type matrix, the bloom-by-Type clause |
| 2 | fb421's dead knobs restored | `WIDEN_MUT_DEADKNOBS` | **1377 / 150** | **+62** | matrix cells at **exactly 0.000** change per quarter, now on all eight Characters of each affected Type |
| 3 | fb421's ceilings restored | `WIDEN_MUT_POLITE` | **1492 / 107** | **+19** | R11 `Amount` on Twin + Twofold across their Characters, matrix cells, the chorus boundary on Twin |
| 4 | every smoother → tau 0 | `WIDEN_MUT_NOSMOOTH` | **1503 / 96** | **+8** | 7 click gates, up to **×488** of the static floor |
| 5 | delay/gain/pan glide removed | `WIDEN_MUT_NOGLIDE` | **1505 / 94** | **+6** | 5 click gates, up to **×488** |
| 6 | the fade-swap-recover dip removed | `WIDEN_MUT_NODIP` | **1507 / 92** | **+4** | all 4 discrete-switch gates, **+38 dB** worse than the bar |
| 7 | the Voices floor of 3 removed | `WIDEN_MUT_NOFLOOR` | **1473 / 118** | **+30** | the label-vs-count gates, the output-side chorus gates, and the `Voices` matrix rows on every Character |
| 8 | the fb421 ±0.97 allpass clamp restored | `WIDEN_MUT_APCLAMP` | **1504 / 95** | **+7** | the sample-rate invariance gate at 96 kHz |
| 9 | one fb423 rename reverted **in the header only** | `WIDEN_MUT_STALENAME` | **1507 / 92** | **+4** | 4 gates in §S: the shipped-corpus gate, the RENAMES-was-applied gate, and **both** drift gates |
| 10 | the `Hear Mono` fold deleted | `WIDEN_MUT_NOMONO` | **1461 / 138** | **+50** | the §Q fold gate on **all 48 cells** (it ran on one at fb424) plus the viz gate — 149 FAIL lines in that log |
| 11 | the `Hear Mono` 15 ms fade → a hard cut | `WIDEN_MUT_MONOSNAP` | **1510 / 89** | **+1** | the §Q click gate, at **×147** the static floor — a single, precisely-targeted gate, which is what a targeted mutation should cost |
| **12** | **fb424's LFO-only `fireRetrig()`** | `WIDEN_MUT_RETRIGLFO` | **1495 / 104** | **+16** | **23 `Retrig` cells go BIT-IDENTICAL** — 7 `Twofold`, 8 `Blur`, 8 `Bands`. Under fb424's cert: **0 gates** |
| **13** | **`Swap` + `Gather` collapsed into `Straight`** | `WIDEN_MUT_FLATFIELD` | **1462 / 137** | **+49** | **144 identical `Field` pairs**, all 48 §V cells. Under fb424's cert: **0 gates** |

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

## 12 · `WIDEN_MUT_RETRIGLFO` — fb424's retrigger, which only knew about the LFO

`fireRetrig()` restored to `res_[v] = sin(2π·ph_[v]); ph_[v] = 0; q_[v] = 0;` and nothing else —
i.e. exactly the shipping code of fb424. The walks (`wk_`, `wkp_`, `wkb_`, `wkg_`) and the two
rotators (`blurRot_`, `gridRot_`) are left alone.

**This is the defect the FIXES brief named, reproduced on demand.** `ph_[]` is read by
`procVoices`' mod-0 branch and by `procTwin`, and by nothing else. `Twofold` (mod 3) runs a walk,
`Blur` runs an allpass field driven by a rotator, `Bands` runs a crossover grid driven by a
rotator. Firing the pill on any of them is a no-op **to the bit**.

| gate | shipping | RETRIGLFO | verdict |
|---|---|---|---|
| `Retrig` is BIT-ALIVE on every cell | **0** dead cells | **23** dead cells | RED |
| ...on `Twofold` | 8 of 8 alive | **7 of 8 BIT-IDENTICAL** | RED |
| ...on `Blur` | 8 of 8 alive | **8 of 8 BIT-IDENTICAL** | RED |
| ...on `Bands` | 8 of 8 alive | **8 of 8 BIT-IDENTICAL** | RED |
| ...on `Throng` / `Twin` / `Steady` | alive | **still alive** — they read `ph_[]`, so the LFO-only retrigger is enough | correct |
| every experiment was CONTROLLED | 0 contaminated | **0 contaminated** | the experiment is still an experiment |

Pasted, from `widen_mut_RETRIGLFO.log`:

```
  FAIL  `Retrig` restarts the mechanism on Twofold/Lilt      post-edge 0.00 vs bar 18.37 (0.20 x Rate-quarter 91.87) · Mix-quarter 43.71 for scale  [BIT-IDENTICAL — the pill does NOTHING here]
  FAIL  `Retrig` restarts the mechanism on Blur/Smooth Six   post-edge 0.00 vs bar 17.33 (0.20 x Rate-quarter 86.66) · Mix-quarter 52.72 for scale  [BIT-IDENTICAL — the pill does NOTHING here]
  FAIL  `Retrig` restarts the mechanism on Bands/Coarse      post-edge 0.00 vs bar 1.21 (0.20 x Rate-quarter 6.06) · Mix-quarter 13.90 for scale  [BIT-IDENTICAL — the pill does NOTHING here]
  FAIL  `Retrig` is BIT-ALIVE on every cell not on the known-inert roster  23 dead cells, first: Twofold/Lilt
  ok    every experiment was CONTROLLED (bit-identical before the edge)     0 contaminated cells
```

> 🔑 **`Twofold`/`Static Pair` is the 8th cell, and it stays ALIVE under this mutation** — because
> fb425 also gave that Character the constant-ratio reader it always claimed to have, and
> `fireRetrig()` resets `q_[]`, the reader's own phase, in *both* versions. That is the honest
> reading: this mutant proves the WALK/ROTATOR half of the fix, and nothing more.
>
> 🔑 **`post-edge 0.00` is an exact zero, not a small number.** The pre-edge control is bit-identical
> by construction, so a 0.00 after the edge means the pill produced no sample of difference at all.
> Under fb424's cert this mutation moves **no gate whatsoever**: the only thing that touched the
> pill was a click test, on Type 0, and a click test cannot see silence.

---

## 13 · `WIDEN_MUT_FLATFIELD` — two of the six `Field` options collapsed to `Straight`

`applyField()`'s `case 3` (`Swap`) and `case 5` (`Gather`) become `break;`. The dropdown still
**publishes six names**; two of them are now the same physics as the first.

**This is the skeptic's deletion, made reproducible.** Under fb424's cert it moved **zero lines** —
`Field` had a naming assertion, a mono tag check and a click test, and nothing that compared the
options to each other.

| gate | shipping | FLATFIELD | verdict |
|---|---|---|---|
| NO two `Field` options are IDENTICAL anywhere | **0** identical pairs / 720 | **144** identical pairs | RED |
| every `Field` pair clears a fifth of a `Width` quarter-turn | **1** weak cell / 48 | **48** weak cells | RED |
| the Field roster is the 6 options the engine publishes | ok | **ok** — and that is the point: the *names* are intact, only the physics is gone | correct |

Pasted, from `widen_mut_FLATFIELD.log`:

```
  FAIL  Field: all 15 pairs distinct on Throng/JP Classic    closest 0.00 (bar 13.96 = 0.20 x Width-quarter 69.82) — Straight vs Swap
  FAIL  Field: all 15 pairs distinct on Throng/Even Fan      closest 0.00 (bar 13.01 = 0.20 x Width-quarter 65.05) — Straight vs Swap
  FAIL  NO two `Field` options are IDENTICAL anywhere in the 48-cell matrix  144 identical, first: Straight == Swap on Throng/JP Classic
  FAIL  every `Field` pair clears a FIFTH of a Width quarter-turn, on every cell  48 weak cells; closest anywhere Throng/JP Classic Straight vs Swap at 0.00
```

> 🔑 **144 = 48 cells × 3 pairs** (`Straight`≡`Swap`, `Straight`≡`Gather`, `Swap`≡`Gather`).
> The count is exactly what the arithmetic predicts, which is itself a check that the gate is
> counting pairs and not artefacts.
>
> 🔑 **The metric had to be CHANNEL-ORDERED to catch this.** `Swap` exchanges L and R: correlation
> is unchanged, side/mid is unchanged, the mono sum is unchanged, and any metric built on those
> three reads `Straight` and `Swap` as the same option **by construction**. §V compares 30 log
> bands of L *and* 30 of R as an ordered vector, which is why `Straight == Swap` shows up as an
> exact 0.00 rather than not showing up at all.

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
