# MUTATION.md — COMPRESS · OTT · DynamicsCore

**FIXES.md §0.** Every law-1, law-4 and R11 gate on these two devices has been run against a
deliberately broken copy of the engine, and the failing output is pasted below.

**fb423 adds the two things §0 said were still missing anywhere in the family: an R11 mutant per
device, and a DEAD-KNOB mutant per device. It also adds a mutant for the names gate itself.**

Produced by `python3 mutate.py`. It copies `DynamicsCore.h`, `TerrainCompressFx.h`,
`TerrainOttFx.h`, `dynamics_cert.cpp`, `shipped_labels.inc`, **both worklets and `ROSTER.md`**
(cert §1 now gates that downstream still says what the headers say, and reads them off disk) into
a scratch directory, deletes
**one mechanism** with an exact string replacement — and **aborts if the text it expects is not
there**, so a mutation can never silently fail to apply — rebuilds the cert against the mutant,
runs only the sections that matter, and requires the named gate to turn `FAIL`.

> A mutation that does not apply produces a false green, which is the disease this file treats.
> Every replacement is asserted to match exactly once.

---

## The table

| mutant | mechanism deleted | gate that fired | REAL engine | MUTANT |
|---|---|---|---|---|
| `compress-smoother-seed` | `seedShape()` — the new smoother is not handed the live `gr_` | §4c3 GR continuity, 64 Type changes | **0.86 dB** | **17.44 dB** |
| `compress-smoother-seed+noslew` | ↑ *and* the transition slew limiter | §4d all 64 Type transitions | **1.23 dB/ms** | **10.34 dB/ms** |
| `compress-transition-slew` (alone) | the 1.5 dB/ms transition slew limit | — | 1.23 / 1.00 dB/ms | 1.94 / 1.25 dB/ms — **SURVIVED** |
| `compress-heat-kind-fade` (alone) | the waveshaper-kind crossfade snaps instead | **§4c4 gain-element CURVATURE, 56 Type changes** | **2.02 dB** | **23.89 dB** |
| `compress-colour-drive-smoothing` | the gain element's drive follows the RAW ballistic state | §4d all 64 Type transitions | **1.23 dB/ms** | **2.11 dB/ms** |
| `compress-clip-ceiling-tracking` | the soft-clip ceiling comes off the TARGETS again (fb421) | §4d all 448 Character transitions | **1.00 dB/ms** | **5.47 dB/ms** |
| `compress-discrete-fades+noslew` | all ten discrete-rewiring fades set to tau 0 | §4d all 448 Character transitions | **1.00 dB/ms** | **2.82 dB/ms** |
| `compress-sample-rate` | `coefTau` ignores `fs` (hard-codes 48 kHz) | §4g realised t63 at 96 kHz | **−0.0 %** | **−45.5 % attack, −50.1 % release** |
| `compress-detect-ownership` | a Character re-points the detector (`detForce` restored) | §4c2 no Character changes the rectifier | 0 overrides | **24 overrides** |
| `ott-tree-swap-fade` | back to fb421's instant `dip_ = 0.0f` | §6c the TREE SWAP | **0.82 dB/ms** | **5.94 dB/ms** |
| `ott-transition-slew` | OTT's 1.5 dB/ms transition slew limit | §6c all 448 Character transitions | **1.39 dB/ms** | **3.02 dB/ms** |
| `ott-mid-side-fade` | the M/S basis rotates in one sample | §6c Stereo Linked → Mid-Side | **0.25 dB/ms** | **2.66 dB/ms** |
| `ott-band-clip-fade` | Heavy's per-band clipper inserts in one sample | §6c all 64 Type transitions | **0.88 dB/ms** | **5.20 dB/ms** |
| `core-floor-gate` | `floorGate()` deleted — returns 1.0 always | §6b(d) a real −96 dBFS floor | **−76.4 dBFS** | **−44.4 dBFS** |
| `cert-fft-normalisation` | the Parseval normalisation dropped (what fb421 shipped) | §2 spectrum is CALIBRATED | **−26.022 dBFS** | **+38.956 dBFS** |
| **fb423 — R11, the last outstanding piece of FIXES.md §0 in the family** | | | | |
| `compress-no-ceiling` | the slope cap becomes a polite **2:1** | §4b(a) 48 dB staircase at Push/Ratio 100 | **DRR 0.0188** | **DRR 0.5069** |
| `ott-no-ceiling` | Amount's top half stops closing T_up onto T_dn | §6b(a) Amount 100 vs Amount 50 | **5.38 dB (55 % of the preset)** | **9.70 dB (100 % of it)** |
| **fb423 — law 1, a DEAD KNOB** | | | | |
| `compress-dead-knob (Burn, P8)` | `p.b8` never reaches `heatTgt_` | §4 the P8 0→100 sweep | **span 32.37** | **span 0.00** |
| `ott-dead-knob (Treble, P8)` | `p.b8` never reaches `trim[2]` | §6 the P8 0→100 sweep | **span 23.93** | **span 0.00** |
| **fb423 — the new mechanisms and the new gates** | | | | |
| `ott-sheen-upward-lane` | Sheen's high band gets Over Top's thresholds back | §5 BOTH Sheen gates (and §5c) | **−4.00 dB lift · 15 dB of knee** | **+8.53 dB clamp · −3 dB of knee** |
| `names-downstream-drift` | one string in `ott-worklet.js` drifts off the header | §1 ott-worklet CHARS == charNames() | 64 strings, exact | **[46] downstream `Long Ears` vs header `Mean Ears`** |

**21 mutants · 20 gates fired · 1 survivor.**

> A row was REMOVED this round, and removing a mutant needs a reason as much as adding one:
> `compress-heat-kind-fade+noslew` deleted TWO mechanisms and still only reached a LEVEL gate,
> where it survived. `compress-heat-kind-fade` (alone) now fires on §4c4. The two-mechanism
> version proves strictly less than the one-mechanism version, so it is gone.

---

## Pasted output

```
mutant                             gate that must fire                    result
------------------------------------------------------------------------------------------------------------
compress-smoother-seed             GR is continuous across all 64 Type changes RED (good)
        FAIL  GR is continuous across all 64 Type changes (≤ 2 dB in the first 0.17 ms) worst 17.44 dB  (Limit → Opto)
compress-smoother-seed+noslew      Type transitions                       RED (good)
        FAIL  all 64 Type transitions ≤ 2.0 dB of gain moved in 1 ms worst 10.34 dB/ms  (Exact → Opto)   [fb421 engine: 17.36]
compress-transition-slew (alone)   transitions                            *** SURVIVED ***
compress-colour-drive-smoothing    transitions                            RED (good)
        FAIL  all 64 Type transitions ≤ 2.0 dB of gain moved in 1 ms worst 2.11 dB/ms  (Vari-Mu → Limit)   [fb421 engine: 17.36]
compress-clip-ceiling-tracking     transitions                            RED (good)
        FAIL  all 448 Character transitions ≤ 2.0 dB of gain moved in 1 ms worst 5.47 dB/ms  (Limit: Loud War → Pump Limit)   [fb421 engine: 16.26]
compress-discrete-fades+noslew     transitions                            RED (good)
        FAIL  all 448 Character transitions ≤ 2.0 dB of gain moved in 1 ms worst 2.82 dB/ms  (Ride: Only Up → Fast Clamp)   [fb421 engine: 16.26]
compress-sample-rate               REALISED t63                           RED (good)
        FAIL  96.0 kHz: REALISED t63 within 12 % of 48 kHz         attack 3.000 ms (-45.5 %), release 55.6 ms (-50.1 %), settled GR 21.78 dB vs 21.74
compress-detect-ownership          no Character changes the rectifier     RED (good)
        FAIL  no Character changes the rectifier at any Detect setting 24 overrides, first: Exact · Loose Grip overrides Detect=Peak
ott-tree-swap-fade                 TREE SWAP                              RED (good)
        FAIL    the TREE SWAP Over Top → Two Band (3 bands → 2) 5.94 dB/ms   [fb421 engine: 5.95 — an instant dip_ = 0.0f]
ott-transition-slew                Character transitions                  RED (good)
        FAIL  all 448 Character transitions ≤ 2.0 dB of gain moved in 1 ms worst 3.02 dB/ms  (Heavy: Fast Grind → Wall Ears)   [fb421 engine: 3.02]
ott-mid-side-fade                  Mid-Side                               RED (good)
        FAIL    Stereo Linked → Mid-Side                         2.66 dB/ms
ott-band-clip-fade                 Type transitions                       RED (good)
        FAIL  all 64 Type transitions ≤ 2.0 dB of gain moved in 1 ms worst 5.20 dB/ms  (Surge → Heavy)   [fb421 engine: 5.97]
core-floor-gate                    -96 dBFS floor comes OUT at            RED (good)
        FAIL  (d) a REAL dithered -96 dBFS floor comes OUT at      -44.4 dBFS (+51.64 dB of lift)  ← must stay inaudible
compress-heat-kind-fade (alone)    CURVATURE is continuous                RED (good)
        FAIL  the gain element's CURVATURE is continuous across all 56 Type changes worst 23.89 dB of curvature in 2 ms  (FET 76 → OverEasy)
compress-no-ceiling                48 dB staircase                        RED (good)
        FAIL  (a) 48 dB staircase → ≤ 5 % survives at Push/Ratio 100 DRR 0.5069  (bypassed control 1.0000)
ott-no-ceiling                     Amount 100 leaves                      RED (good)
        FAIL  (a) Amount 100 leaves ≤ 65 % of what Amount 50 leaves probe 22.78 dB → 9.70 dB at Amount 50 (= the Ableton preset) → 9.70 dB at 100 (100 % of it)
compress-dead-knob (Burn, P8)      (P8)                                   RED (good)
        FAIL    Burn (P8) — via added THD                        span 0.00 · monotone ↑
ott-dead-knob (Treble, P8)         (P8)                                   RED (good)
        FAIL    Treble (P8) — 8-12 kHz                           1.22 → 1.22 (span 0.00) · monotone
ott-sheen-upward-lane              Sheen                                  RED (good)
        FAIL  Sheen: 6 dB into a decay its high band LIFTS while Over Top's still CLAMPS signed high-band GR: Sheen +8.53 dB (lift), Over Top +6.68 dB (clamp)
        FAIL  Sheen: its high-band UPWARD knee sits at the PROGRAMME, Over Top's far below it net lift begins at -21 dB of input for Sheen, -18 dB for Over Top (-3 dB apart, bar 9)
        FAIL  every Character ≥ 2× JND from its Type's default  weakest 1.11× JND  (Sheen · Fast Shimmer), 1 below bar
names-downstream-drift             ott-worklet CHARS                      RED (good)
        FAIL  ott-worklet CHARS == charNames()                     [46] downstream 'Long Ears' vs header 'Mean Ears'
cert-fft-normalisation             CALIBRATED                             RED (good)
        FAIL  spectrum is CALIBRATED: a −26.02 dBFS sine reads its own level reads 38.956 dBFS (true -26.021)
------------------------------------------------------------------------------------------------------------
21 mutants, 20 gates fired, 1 SURVIVORS  → compress-transition-slew (alone)
```

---

## fb423 — ONE OF THE TWO SURVIVORS IS CLOSED, and how

`compress-heat-kind-fade` survived because **every gate it faced was a LEVEL gate**. §4d measures
dB of gain per millisecond. The mechanism it protects is the SHAPE of the gain element's transfer
curve, and a waveshaper whose curve changes in one sample at constant gain is invisible to every
level metric there is — that is not a subtle point, it is the definition of a waveform artifact.

Cert **§4c4** measures the **curvature**:

    S = H3_dB - 3 * H1_dB       for y = x + c*x^3:  H1 ~ A, H3 ~ (c/4)A^3  =>  S = 20*log10(c/4)

independent of drive to cubic order. Sampled in the 2 ms either side of the switch on a 2 kHz tone
with `Burn` at 100. **Real engine 2.02 dB (Types) / 4.36 dB (Characters), bar 6. Mutant 23.89 dB.
Control (same config both sides) 0.00 dB.**

🔬 Two earlier drafts of that gate were level gates in disguise, and both are recorded in the cert
source so nobody rebuilds them: absolute 3rd-harmonic level (goes as A^3) read **12.08 dB** on
`Ride: Only Up -> Slow Iron`, and H3/H1 (goes as A^2) read **10.69 dB** on the same pair — a
transition whose GR legitimately travels 0 -> 15.6 dB over 60 ms at 0.35 dB/ms, comfortably inside
§4d's bar. Section K plants a pure +6.02 dB gain step and a x4 curvature change: the invariant
reads **0.204 dB** and **11.84 dB** (theory 12.04). It also prints the honest limit of the
blindness — at engine-like drive a pure gain step still leaks **2.12 dB** into S, which is why the
bar is 6 dB and not 1.

**`compress-transition-slew` is STILL a survivor**, and §4c4 cannot close it *by construction*:
it is a LEVEL mechanism and the shape invariant cancels level on purpose. See below.

## The remaining survivor — stated plainly, not buried

One of the two is closed (above). The one that is left is not a hole in the *device*: it is a
mechanism that turned out to be **redundant** — another mechanism already holds the gate green
without it, so deleting it changes nothing a gate can see. That is still a survivor by §0's
definition and it is reported as one.

**1. `compress-transition-slew` — the 1.5 dB/ms transition slew limit, alone.**
Removing it takes the worst Type transition from **1.23 → 1.94 dB/ms** and the worst Character
transition from **1.00 → 1.25**. Both stay under the 2.0 bar, so no gate fires. It *was*
load-bearing when it was written — at that point the worst cell was 3.97 dB/ms — and two later
fixes (the gain element's drive following a smoothed GR, and the soft-clip ceiling tracking the
applied gain rather than the target) removed the need for it. It is now 0.71 dB of margin, not a
mechanism the gates depend on.
**Disposition: KEPT, and honestly labelled.** Deleting it would leave the worst cell 0.06 dB
under the bar, which is not a margin I would ship. The right fix if this is unacceptable is to
delete it and re-voice, not to invent a gate that only it can pass.

**2. `compress-heat-kind-fade` — CLOSED at fb423.** See the section above. What it took was the
gate described here last round: the gain element's own continuity rather than the applied gain's.
Forty lines was about right; the part that was not obvious in advance is that it had to measure
CURVATURE, because the first two drafts of it measured harmonic LEVEL and were §4d with a 3x
multiplier bolted on.

**What it would take to close the one that is left:** nothing honest. Its only observable is
dB/ms, §4d is the dB/ms gate, and it holds 0.71 dB of margin there. A gate that could fail without
it would need a bar between 1.23 and 1.94 dB/ms — a gate only this mechanism can pass, which is
exactly what §3.2 forbids. The right move if 1.94 is unacceptable is to delete the limiter and
re-voice the transitions, not to invent the gate.

---

## What the mutation round found that the cert did not

Running the mutants **changed two gates**, both of which had been written wrong:

1. **The floor gate defeated itself.** Version 2 isolated the upward computer by subtracting the
   lift of a −110 dBFS bed ("makeup only"). Delete `floorGate` and the −110 dBFS reference *also*
   gets the full upward lift — the two numbers move together and the difference reads **−2.11 dB**,
   green, on an engine with no floor gate at all. *A reference computed through the mechanism
   under test is not a reference.* The bar is absolute now: a −96 dBFS bed must come **out** below
   −70 dBFS. Real engine −76.4; mutant −44.4.
2. **The smoother-seed gate was too weak to be evidence.** With the transition slew limiter also
   present, deleting the seed left the click matrix at 2.04 dB/ms — over the bar, but by 0.04 dB
   and on one cell out of 512. Defence in depth is good engineering and terrible evidence. §4c3
   now measures the seed and nothing else — `gr_` is the same physical quantity in all five
   smoother shapes, so it must be continuous across a shape change — and reads **0.86 dB** on the
   real engine against **17.44 dB** on the mutant.

---

# fb425 — nine new mutants for the full-matrix round

None of these could have fired before this round: sections 7/8/9 did not exist, and the three
section-1 gates three of them aim at were a substring search, a hand-typed blacklist, and a list
with no cardinality.

| # | mutant | mechanism deleted | gate that must fire |
|---|---|---|---|
| 1 | `ott-clip-ceiling-backwards` | the fb424 per-band ceiling: `db2lin (Tdn + clipHd_ + mkDb_[b])`, and the clip blend not scaled by the slope | §8d `Amount` 0 is neutral and the knob runs forwards, all 64 cells |
| 2 | `compress-ratio-wall` | the feedback→feedforward crossover over the last 10 % of `Ratio` | §7b every Type walls at Push/Ratio 100 (R11) |
| 3 | `ott-mix-forced-wet` | `mixTgt_ = 1.0f` — **the exact mutation that left all 53 fb424 OTT gates green** | §8b Mix 0 is the DRY path on every Type |
| 4 | `ott-crest-pill-noop` | `upHold_ = (cs.upHold != 0)` — the front pill becomes a no-op | §8 no control is BIT-IDENTICAL at 0 and 100 |
| 5 | `ott-sample-rate` | `fs_` dropped from the band followers (`nA`/`nR`) | §8e the REALISED ballistics match 48 kHz |
| 6 | `compress-dead-cell (Release, Vari-Mu only)` | the Release knob killed on **ONE TYPE** — §4's sweep runs on Type 0 and stays green. This is the fb424 level, deleted. | §7 the matrix |
| 7 | `roster-grid-scramble` | two Characters moved under the **wrong Types** in ROSTER.md — the fb424 substring search stayed green | §1 ROSTER.md: no Character appears under the WRONG Type |
| 8 | `retired-label-drift` | a retired label (`Full Bite`) put back downstream as a live label | §1 no RETIRED label survives as a LABEL downstream |
| 9 | `exemption-not-load-bearing` | an exemption entry that exempts nothing — the shape `Auto` had before fb423 and `Peak`/`Bass`/`Treble`/`Ratio` had until this round | §1 every exemption is LOAD-BEARING |

Run with `python3 mutate.py` (now accepts a substring filter: `python3 mutate.py band-clip`).

## The fb425 run — 30 mutants, 27 fired, 3 survivors

```
compress-smoother-seed             GR is continuous across all 64 Type changes RED (good)
        FAIL  GR is continuous across all 64 Type changes (<= 2 dB in the first 0.17 ms) worst 17.44 dB  (Limit -> Opto)
compress-smoother-seed+noslew      Type transitions                       RED (good)
        FAIL  all 64 Type transitions <= 2.0 dB of gain moved in 1 ms worst 10.37 dB/ms  (Exact -> Opto)
compress-transition-slew (alone)   transitions                            *** SURVIVED *** (known, labelled redundant)
compress-colour-drive-smoothing    448 Character changes                  RED (good)
        FAIL  ... and across all 448 Character changes             worst 28.35 dB  (Ride: Only Up -> Fast Clamp)
compress-clip-ceiling-tracking     transitions                            RED (good)
compress-discrete-fades+noslew     transitions                            RED (good)
compress-sample-rate               REALISED t63                           RED (good)
compress-detect-ownership          no Character changes the rectifier     RED (good)
ott-tree-swap-fade                 TREE SWAP                              RED (good)
ott-transition-slew                Character transitions                  RED (good)
ott-mid-side-fade                  Mid-Side                               RED (good)
ott-band-clip-fade (alone)         Type transitions                       *** SURVIVED ***  <- fb425 REGRESSION, diagnosed below
ott-band-clip-fade + slope-scaled blend Type transitions                  *** SURVIVED ***  <- and both together
core-floor-gate                    -96 dBFS floor comes OUT at            RED (good)
compress-heat-kind-fade (alone)    CURVATURE is continuous                RED (good)
compress-no-ceiling                48 dB staircase                        RED (good)
ott-no-ceiling                     Amount 100 leaves                      RED (good)
ott-no-ceiling (per-Type, §8c)     walls at Amount 100                    RED (good)
        FAIL  every Type walls at Amount 100 (R11): static AND dynamic 8 of 8 red, first: Over Top
compress-dead-knob (Burn, P8)      (P8)                                   RED (good)
ott-dead-knob (Treble, P8)         (P8)                                   RED (good)
ott-sheen-upward-lane              Sheen                                  RED (good)
names-downstream-drift             ott-worklet CHARS                      RED (good)
ott-clip-ceiling-backwards         runs forwards                          RED (good)
        FAIL  Amount 0 is neutral and the knob runs forwards, all 64 cells 10 cells backwards,
              first: Heavy / Welded Shut: THD 34.23 % at 0 vs 2.34 % at 100
compress-ratio-wall                walls at Push/Ratio 100                RED (good)
        FAIL  every Type walls at Push/Ratio 100 (R11)             3 of 8 red, first: FET 76
ott-mix-forced-wet                 Mix 0 is the DRY path                  RED (good)
        FAIL  Mix 0 is the DRY path on every Type ... worst 9.446 dB from the input (bar 0.35)  (Surge)
ott-crest-pill-noop                BIT-IDENTICAL                          RED (good)
        FAIL  no control is BIT-IDENTICAL at 0 and 100 in any unruled cell 63 DEAD cells,
              first: Crest @ Over Top / Straight Up
ott-sample-rate                    REALISED ballistics                    RED (good)
        FAIL  96.0 kHz: the REALISED ballistics match 48 kHz  attack area 416 dB.ms (-11.5 %),
              release area 12857 dB.ms (+53.2 %)  (bar 12 %)
compress-dead-cell (Release, Vari-Mu only) under the bar                  RED (good)
        FAIL  every unruled cell moves >= 0.5 dB end to end  2 under the bar,
              first: Release @ Vari-Mu/Time Four - the ruling is stale
        (and section 4's OWN Release sweep, which runs on Type 0, stayed GREEN: "span 11.39,
         monotone" - which is precisely the fb424 blindness this round exists to close)
roster-grid-scramble               WRONG Type                             RED (good)
        FAIL  ROSTER.md: no Character appears under the WRONG Type 2 misplaced:
              FET 76: `Blackface` is not in its grid row, in order
retired-label-drift                RETIRED label survives                 RED (good)
        FAIL  no RETIRED label survives as a LABEL downstream 1 hits, first: Full Bite
exemption-not-load-bearing         LOAD-BEARING                           RED (good)
        FAIL  every exemption is LOAD-BEARING 1 exempt nothing, first: Grip
cert-fft-normalisation             CALIBRATED                             RED (good)
```

## The one fb425 REGRESSION in this table, reported not hidden

`ott-band-clip-fade` **fired last round and survives now, alone and paired.** The diagnosis, and it
is a consequence of the ceiling fix rather than a defect:

the fb425 ceiling rides the **realised** band output (`Ldn + gdb + clipHd_`), so switching the
clipper in or out only changes peaks that sit a few dB above the envelope — a bounded change, not a
step. Deleting the 20 ms `gClip_` fade AND the slope-scaled blend still measures under the click
bar. A gate was added for this specifically (`Heavy → Sheen` and `Band Clip → No Clip` **at Amount
100**, where the clipper is doing 2.34 % THD, rather than at the default Amount 50 where the
fb425 ceiling leaves it nearly idle) and both read 0.40 / 0.69 dB/ms on the real engine and stay
under the bar on the mutant too.

So: **the fade in front of the OTT band clipper is no longer load-bearing.** It is kept — it costs
one multiply and it protects the case where a future Character sets `clipHead` very low — but it is
recorded here as REDUNDANT, not as proven, exactly like `compress-transition-slew`. The clipper
itself is still proven audible by §5's `Heavy` discriminator, by the `Band Clip` / `No Clip`
Character distinctness, and by §8d's THD table (2.34 % at Amount 100 against 0.00 % at Amount 0).
