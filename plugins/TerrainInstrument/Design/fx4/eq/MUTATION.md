# EQUALIZER — MUTATION.md  (FIXES.md §0)

**Every law-1, law-4 and R11 gate in `eq_cert.cpp`, run against a deliberately broken copy of
`TerrainEqualizerFx.h`, with the real before/after numbers pasted.**

Reproduce:

```
Design/fx4/eq/run_mutations.sh /tmp/eqmut      # builds 1 baseline + 10 mutants, runs all 11
```

The mutations are `#ifdef` hooks inside the engine header (`EQ::mutationTag()` lists them and the
cert prints a red banner when one is active). With no `-D` flag not one branch of the file changes.

```
baseline           exit=0  RESULT: 147 pass,  0 FAIL   <- fb425: §Q added (12 gates), §P widened
EQ_MUT_NO_PIVOT    exit=1  RESULT: 138 pass,  9 FAIL
EQ_MUT_NO_RINGCAP  exit=1  RESULT: 146 pass,  1 FAIL
EQ_MUT_NO_SMOOTH   exit=1  RESULT: 145 pass,  2 FAIL
EQ_MUT_NO_DIP      exit=1  RESULT: 146 pass,  1 FAIL
EQ_MUT_NO_CEILING  exit=1  RESULT:  89 pass, 58 FAIL
EQ_MUT_NO_DENORM   exit=1  RESULT: 146 pass,  1 FAIL
EQ_MUT_DEAD_CELL   exit=1  RESULT: 145 pass,  2 FAIL   <- fb425, and ONLY §Q sees it
EQ_MUT_POLITE_CELL exit=1  RESULT: 146 pass,  1 FAIL   <- fb425, and ONLY §Q sees it
EQ_MUT_MIX_WET     exit=1  RESULT: 139 pass,  8 FAIL   <- fb425
EQ_MUT_FLAT_FOCUS  exit=1  RESULT: 144 pass,  3 FAIL   <- fb425, one dropdown OPTION deleted
EQ_MUT_POINT_CURVE exit=1  RESULT: 156 pass,  3 FAIL   <- fb452, the DISPLAY mutant (below)
```

## fb452 — `EQ_MUT_POINT_CURVE`, the first mutant that breaks nothing you can hear

Max, on the shipped fb449: *"it bounces up and down like a goofball when I drag a sharp notch."*
The audio was perfect and every one of the 154 gates was green, because none of them watched the
drawn curve MOVE. `pushCurve()` point-sampled 96 log bins; a bell narrower than a bin (0.104
octave) sat between two samples, so the card drew whatever it happened to catch and the depth
flickered as the notch walked. Measured with `Tests/eq_bounce.cpp`: **10.63 dB of bounce**, which
is 11.7 px on the card at `fx4DbY`'s 1.1 px/dB.

More bins alone does not fix it — measured, not assumed:

```
bins    bounce over one decade, Q x8 cut
  96    10.63 dB   (11.7 px)   <- what shipped as fb449
 192     5.59 dB    (6.1 px)   <- the "obvious" fix, and still a goofball
 288     3.35 dB    (3.7 px)
 384     2.17 dB    (2.4 px)
1536     0.17 dB    (0.2 px)   <- point sampling only gets honest here: 16x the 60 Hz wire
```

The fix decimates a fine sub-grid **by truth instead of by position**: 192 bins x 9 sub-frequencies,
and a bin reports its CENTRE wherever the response is monotone across it (the old contract, kept
bit-exact) and the local EXTREMUM wherever it actually contains one. Bounce after: **0.14 dB**.
Cost, wall-clocked over 60 s of audio through the busiest 8-stage cascade: **0.069 % -> 0.095 %**
of one core, because the cascade is now product-accumulated in linear and logged once per point
instead of once per stage — 18x the resolution for a quarter of a percent.

`-DEQ_MUT_POINT_CURVE` restores exactly the fb449 sampler and nothing else, so the audio is
bit-identical and every DSP gate stays green. The three gates it must turn RED:

```
FAIL  a SHARP notch DRAGGED across the axis holds its drawn depth    5.62 dB of bounce (bar 0.5)
FAIL     ... and that depth is REAL: a sine swept through that bin   drawn -27.33, sine -29.92 dB
FAIL     ... same for a sharp BOOST: the drawn peak is a peak        drawn  27.33, sine  29.92 dB
```

**The baseline is 147 pass / 0 FAIL.** (fb422 left one pre-existing §O failure — two unruled
names — and fb423's second ruling closed it; every count above is the mutant's own damage, with
nothing carried. The line that used to say "every mutant carries the baseline's 1 pre-existing
§O failure" was stale prose from fb422 and is deleted.)

---

## fb425 — THE FOUR NEW MUTANTS, and why they exist

`M1`–`M6` each delete a whole MECHANISM, so a gate that samples one cell still sees them.
`M7`–`M10` are the mutants a *sampled* gate cannot see: two of them break exactly **one cell of
the 7 × 8 matrix** and leave the other 55 bit-perfect, and one deletes a single dropdown OPTION
while leaving its label on the card. They are the fb424 blindness, reproduced deliberately so §Q
can be shown to close it.

| # | mutation | cells broken | gates that fire | gates that STAY GREEN |
|---|---|---|---|---|
| M7 | `EQ_MUT_DEAD_CELL` — `Trait` is ignored on **Open × `Soft Knee` only** | 1 of 672 | **§Q1** ×2 | §F, **§F2's own Open row** (it probes `Gloss`), §K, everything else |
| M8 | `EQ_MUT_POLITE_CELL` — band gains clamped to ±10 dB on **Passive × `Deep Atten` only** | 1 of 56 | **§Q3** ×1 | **all seven §K ceiling gates** (they probe Surgical/Chisel) |
| M9 | `EQ_MUT_MIX_WET` — `mixTg_` forced to 1.0, the Mix knob ignored | every cell | §A, §F, §I ×2, **§Q1, §Q2 ×2** | — |
| M10 | `EQ_MUT_FLAT_FOCUS` — the Focus option **`Side` silently plays `Stereo`**, label unchanged | 1 of 5 options, every Type | §H, §J3, **§Q4** | §A's four Focus gates: bit-exact pass-through, M/S round trip — all still green |

```
M7  FAILED: no knob is BIT-IDENTICAL at 0 % and 100 % in ANY of the 672 knob-cells
            [1 dead knob-cells:  Trait @ Open x `Soft Knee`]
M7  FAILED: every one of the 672 knob-cells moves the output spectrum by >= 3.0 dB
            [1 cells under the bar:  Trait @ Open x `Soft Knee` 0.000 dB]

M8  FAILED: every UNCAPPED cell reaches §K's own ceiling bar (MSD >= 55 dB)
            [52 of 53 uncapped cells  UNDER: Passive x `Deep Atten` 10.8 dB]

M9  FAILED: Mix 0 % is the DRY signal BIT-EXACTLY, in all 56 cells   [0 of 56 cells bit-exact]
M9  FAILED: Mix 50 % is the exact LINEAR midpoint (null <= -60 dB), in all 56 cells
            [0 of 56 cells; worst 0.0 dB at Surgical x `Plain`]
M9  FAILED: no knob is BIT-IDENTICAL ... [56 dead knob-cells: Mix @ every Type x Character]
```

```
M10 FAILED: every Focus pair separates by >= 1.5 dB on every Type (70 comparisons)
            [0 of 7 Types; worst pair 0.00 dB = Surgical Stereo/Side
             UNDER: Surgical 0.00 dB, British 0.00 dB, American 0.00 dB, Passive 0.00 dB,
                    Open 0.00 dB, Dynamic 0.00 dB, Chisel 0.00 dB]
```

**M10 is the `Field` finding in my own back panel.** Every Focus gate this cert had before fb425
was STRUCTURAL — the untouched channel is bit-exact, the M/S round trip is −149.4 dB — and every
one of them stays green while an option is silently gone, because none of them ever compared the
options TO EACH OTHER.

**M7 and M8 are the whole argument for §Q.** Each is a one-cell bug of exactly the kind three
rounds of sampled gates could not see, and in both cases *every gate this cert had before fb425
stays green* — including §F2's own Open row, which probes Open × `Gloss` and therefore walks
straight past a `Trait` knob that is dead one Character away.

---

## fb425 — TWO CONTROLS THAT ARE NOT `#ifdef`s (the downstream gates need mutating too)

A gate over a MARKDOWN file cannot be mutated with a compiler flag, so both new §P gates were
run against a deliberately corrupted copy of their own subject and required to fail.

**C1 — the skeptic's move on `ROSTER.md §3`.** Two Characters swapped *between two Types*, both
strings still present in the file:

```
   swapped `Tight` (Surgical) <-> `Big Knob` (British)

   ok    ROSTER.md names all 89 published labels ...            89 of 89 present   <- P5, BLIND
   FAIL  ROSTER §3 assigns the 56 Characters to the RIGHT Types, IN ORDER
         5 of 7 Type paragraphs match charNames() element for element
         ORDER: Surgical slot 1: ROSTER 'Big Knob' vs header 'Tight',
                British  slot 1: ROSTER 'Tight' vs header 'Big Knob'
         CROSS-TYPE: `Big Knob` (British's) inside the Surgical paragraph,
                     `Tight` (Surgical's) inside the British paragraph
```

P5's substring-presence test passes the corrupted file at 89 of 89 — which is precisely the
finding: presence was never the question. P6 names both halves of the swap.

**C2 — the cert's own retired names.** The two fb424 strings put back into the gate labels they
were actually shipped in:

```
   FAIL  not one of the 29 RETIRED strings survives in ROSTER, worklet OR THE CERT
         2 retired strings still live:
           Width eq_cert.cpp x1 [gate ("Surgical `Width` at 100 % is a resonator, ...]
           Metal eq_cert.cpp x1 [gate ("Amount 0 % nulls BIT-EXACTLY at a full Chisel/Metal patch"...]
```

Both files were restored immediately afterwards and the baseline re-run at 146/0.

---

## The table

| # | mechanism deleted | gate that fired | real engine | mutated engine |
|---|---|---|---|---|
| M1 | `designSlant()` reverts to the fb420 sliding-pivot shelf `designShelf1(f0,−g,+g)` | **§F3** `every Slant probe moves ONE WAY ONLY (wrong-way travel ≤ 0.30 dB)` | **0.00 dB** | **12.00 dB** at 80 Hz, Amount 200 % |
| M1 | " | **§F3** `the 700 Hz PIVOT does not move across the whole Slant sweep` | **0.080 dB** | **45.026 dB** |
| M1 | " | **§F3** `Deep Pivot` / `Bright Pivot` pivot cleanly | 0.291 / 0.056 dB | **21.133 / 21.016 dB** |
| M1 | " | **§F** `Slant LOW end (80 Hz alone — must FALL)` | span 35.3, reversal **0.00** | span 29.6, reversal **5.93** |
| M1 | " | **§F** `Slant TOP end (8 kHz alone — must RISE)` | span 39.4, reversal **0.00** | span 31.7, reversal **5.85** |
| M1 | " | **§K R11** `the LOW end ALONE beats a whole console (≤ −15 dB at 80 Hz)` | **−17.62 dB** | **+5.23 dB** (wrong sign) |
| M1 | " | **§K R11** `Amount 200 % keeps BOTH ends past a console` | −18.72 dB at 80 Hz | **+29.23 dB** at 80 Hz |
| M1 | " | **§K R11** `20 Hz–20 kHz spread ≥ 55 dB` | **68.7 dB** | 30.8 dB |
| M2 | `limitRing()` body + the one-pole `G` clamp (`onePoleGmax`) | **§L** `NOTHING in the device rings longer than 3 s` | **2405 ms**, 0 settings hit the window | **8000 ms — SATURATED**, 26 of 252 settings never decayed |
| M3 | all 11 smoothers τ→0, the per-sample coefficient glide, the Mix smoother | **§J2** `the 11 non-Q params: ≤ 1.0 dB of wet-gain change in ONE sample` | **0.85 dB** (Amount) | **55.18 dB** (Amount) |
| M3 | " | **§J2** `an INSTANT jump costs ≤ 3 dB more than a deliberate sweep` | **+1.26 dB** | **+32.49 dB** (Bite Hz) |
| M4 | the entire Type/Character/Focus fade-swap dip | **§J3** `every switch DIPS — the fade-swap actually ran: dip ≤ −8 dB` | **−23.32 dB** | **−0.07 dB** |
| M5 | the R11 ranges cut to a polite console (±10 dB a band, ±8 Slant, Amount stops at 100 %) | **§K** all seven ceiling gates + 47 more | see §K below | 54 FAIL |
| M6 | the 1e-18 true-zero state flush | **§L** `a Q 90 Chisel ring decays to TRUE zero` | **0.000e+00** | **1.617e-22** after 12 s of silence |

**Every gate this file claims to protect a mechanism with goes RED when that mechanism is
deleted. There are no survivors.**

---

## M1 — `EQ_MUT_NO_PIVOT`, the fb421 blocker, reproduced and then killed

This is the mutation that matters, because it *is* the shipped fb420 code. The `§F3` table is
printed on every run, mutated or not, so the two can be read side by side.

**Mutated (= the fb420 engine), Amount 100 %:**

```
        probe          0%     12%     25%     37%     50%     62%     75%     87%    100% |   travel wrong-way
            80 Hz   23.95   17.95   11.95    5.95    0.00   -5.24   -5.67   -0.70    5.23 |   -18.72      5.93
           120 Hz   23.87   17.87   11.87    5.88    0.00   -4.39   -2.65    2.87    8.84 |   -15.03      5.97
           300 Hz   23.29   17.29   11.29    5.34    0.00   -0.88    3.90    9.81   15.80 |    -7.48      5.99
          2000 Hz   14.52    8.53    2.66   -1.73    0.00    5.52   11.48   17.48   23.48 |     8.96      5.99
          8000 Hz    2.27   -3.58   -7.72   -5.59    0.00    5.97   11.97   17.97   23.97 |    21.70      5.85
         16000 Hz   -7.57  -12.31  -11.32   -5.96    0.00    6.00   12.00   18.00   24.00 |    31.57      4.74
        the fb420 metric (8 kHz MINUS 80 Hz): span 40.4 dB, worst reversal 0.00 dB  <- this is what stayed green
```

120 Hz falls to **−4.39 dB** at 62.5 % and then rises to **+8.84 dB** at 100 % — **13.23 dB of
wrong-way travel**, which reproduces the integration owner's independent sine-transfer number
(−4.75 → +8.55, 13.31 dB; they probed at 65 %, this grid at 62.5 %). At Amount 200 % it is
**+29.23 dB** at 80 Hz — turn it toward the treble and the bass comes back up by 48 dB.

And the last line is the whole lesson: **`span 40.4 dB, worst reversal 0.00 dB`** is bit-for-bit
what the fb420 log printed (`Tilt (spread 8 kHz - 80 Hz)  span 40.4 (need 35) · worst reversal
0.00`). The old gate was not lying. It was measuring a **difference**, and a difference is blind
to a **common-mode** reversal: both ends walk back up together and the difference keeps climbing.

**Real engine, Amount 100 %:**

```
        probe          0%     12%     25%     37%     50%     62%     75%     87%    100% |   travel wrong-way
            80 Hz   17.63   15.37   11.19    5.80    0.00   -5.80  -11.19  -15.37  -17.62 |   -35.25      0.00
           120 Hz   14.53   13.28   10.27    5.53    0.00   -5.53  -10.27  -13.27  -14.52 |   -29.05      0.00
           300 Hz    7.40    7.14    6.23    3.86    0.00   -3.86   -6.22   -7.13   -7.39 |   -14.79      0.00
          2000 Hz   -8.83   -8.46   -7.24   -4.36    0.00    4.36    7.25    8.46    8.83 |    17.66      0.00
          8000 Hz  -19.69  -16.46  -11.56   -5.89    0.00    5.89   11.56   16.46   19.70 |    39.39      0.00
         16000 Hz  -23.31  -17.82  -11.95   -5.99    0.00    5.99   11.95   17.82   23.31 |    46.63      0.00
```

Exactly antisymmetric about the pivot, monotone at every probe, 0.00 dB of wrong-way travel, and
the pivot itself holds to **0.080 dB** across the entire sweep at both Amounts.

**The fix, in one line:** put the shelf's CORNER where the crossing has to land.
`G_corner = G_pivot · 10^(gDb/20)` in the *prewarped* (tangent) domain, giving
`|H|² = (Gp² + s²t²)/(s²Gp² + t²)`, which is exactly 1 at `t = Gp` for every gain and every
sample rate, and whose derivative in `s` has the sign of `t⁴ − Gp⁴`.

---

## M2 — `EQ_MUT_NO_RINGCAP`: the gate that could not fail, and why

fb420's ring gate was **mathematically incapable of failing**, three ways at once:

1. **the ruler saturated below the bar.** `t60ms()` ran a fixed 1.5 s window and returned
   `N*1000/FS` = exactly `1500.0` when the tail never crossed −60 dB. The bar is `3100`.
   `1500 < 3100` for every possible engine.
2. **the coverage never left Amount's default** — but the pole radius of a boost is set by `A·Q`,
   i.e. by the GAIN, so the ceiling is precisely where it has to be swept.
3. **the Slant stage was never in the sweep at all**, and the fixed-pivot fix pushes its real pole
   toward `z = 1` at extreme gain (a 0.6 Hz corner is a 1.7 s decay).

Now: an 8 s window (2.6× the bar), saturation reported *as* saturation, 252 settings
(7 Types × 2 Characters × 2 Amounts × 3 Trait × 3 corner frequencies) plus a separate 32-setting
sweep of the Slant's own pole.

```
real      ok    NOTHING in the device rings longer than 3 s (the pole-radius cap)
                longest T60 2405 ms — Surgical/Four Bells @ 1000 Hz, Trait 1.00, Amount 200 %
                · 0 settings hit the window
          ok    the SLANT's one-pole obeys the same ring law
                longest T60 1841 ms — Deep Pivot, Slant 0 %, Amount 200 %

mutated   FAIL  NOTHING in the device rings longer than 3 s (the pole-radius cap)
                longest T60 8000 ms — Surgical/Tight @ 25 Hz, Trait 0.95, Amount 200 %
                · 26 settings hit the window
```

`8000 ms` is the window, not a measurement — and the cert says so, which is the difference. There
is also a self-check that the ruler can express a failure at all:

```
  ok    (self-check) t60ms REPORTS saturation instead of a number it cannot measure
        a 20 ms window on a 700 ms ring returns 20 ms, saturated=1
```

---

## M3 — `EQ_MUT_NO_SMOOTH`: the skeptic's exact mutation

Deleting all 11 smoothers, the per-sample coefficient glide and the Mix smoother left the fb420
cert at **104 pass / 0 FAIL**. It still leaves the *old* frame-level metric untouched, and that is
worth stating plainly, because it is the honest explanation rather than an excuse:

- `setParams` is **per block**, so even with no smoother at all a 0.8 s sweep still walks the param
  in 128-sample steps of 1/300 of its range;
- the old metric was the second difference of a **256-sample (5.3 ms) frame-level** curve, which
  cannot see a 128-sample staircase;
- and it was scored as *sweeping minus holding* — an **upper bound** that deleting the smoothing
  makes SMALLER, because the coefficient glide is what smears each block's step across the frame.

The replacement measures the artefact per sample, using the identity that a single sinusoid
satisfies `y[n] − 2cos(w)y[n−1] + y[n−2] = 0` exactly and an LTI filter of a sinusoid is still a
sinusoid — so a settled engine reads zero no matter what filter it is running. Normalised, the
number **is the per-sample fractional change of the wet gain**, in dB. Three probe frequencies
(137 / 953 / 5231 Hz), deliberately incommensurate with the 128-sample block so the event lands at
a different phase of each.

```
                     real engine                          EQ_MUT_NO_SMOOTH
param           swept     JUMPED       held  |       swept     JUMPED       held
Low Hz         0.0023     0.0108     0.0018  |      0.3332     3.4335     0.0018
Low            0.0040     0.0431     0.0040  |      0.3070     3.6484     0.0040
Body Hz        0.0141     0.0634     0.0051  |      3.2912    24.0975     0.0051
Body           0.0035     0.3207     0.0016  |      1.8946    14.4535     0.0016
Bite Hz        0.0064     0.1075     0.0048  |      7.0723    39.5631     0.0048
Bite           0.7259     0.6382     0.0016  |     10.4530    30.1954     0.0016
Reach          0.5070     0.6648     0.0037  |     16.4362    39.5557     0.0037
Trait          7.1975     7.9461     0.0837  |     42.8850    48.0031     0.0837
Slant          0.3689     0.3593     0.0175  |     38.0839    45.3593     0.0175
Air            0.1584     0.1829     0.0024  |      7.8674    35.0858     0.0024
Amount         0.1813     0.8484     0.0256  |     34.2459    55.1765     0.0256
Mix            0.0058     0.5223     0.0013  |      2.1512    29.7717     0.0013
```

The **held** column is identical in both builds — that is the control, and it proves the ruler is
zeroed rather than merely small. It is also the polarity fix: fb420 used an instantaneous param
jump as a *"negative control required to FAIL"*, which is backwards. An instant jump is what a host
automation write and a preset recall actually do; absorbing it is what the smoothers are FOR, and
it is now a gate the engine must PASS.

---

## M4 — `EQ_MUT_NO_DIP`: a bar that graded magnitude and not sign

fb420 gated switches at `worstSw <= 20.0` while the gutted build measured **18.62 dB**. It passed
by 1.38 dB, because the bar graded the MAGNITUDE of an excursion and never asked which way it went.
**A fade-swap dips. Deleting it makes the switch step.** Both signs, against the settled level on
*both* sides (so a Type whose new curve is simply louder — Chisel is +19.5 dB louder in this patch
— does not read as a blast):

```
real                switch               pre dB  post dB   dip dB  rise dB
                    Type switch           -8.90    10.63   -23.32     0.91
                    Character switch      -8.90    -8.54   -23.32     0.62
                    Focus switch          -8.90   -26.00   -13.69     0.75
          ok    J3 every switch DIPS — the fade-swap actually ran: dip <= -8 dB   (-13.69 dB)

mutated             Type switch           -8.90    10.63    -0.07     1.77
                    Character switch      -8.90    -8.54    -0.68     0.85
                    Focus switch          -8.90   -25.99    -0.49     0.75
          FAIL  J3 every switch DIPS — the fade-swap actually ran: dip <= -8 dB   (-0.07 dB)
```

---

## M5 — `EQ_MUT_NO_CEILING`: R11 measured, not asserted

Ranges cut to a polite console (±10 dB per band, ±8 dB of Slant, Amount stops at 100 %) —
i.e. every number a real console gives you and no more. **54 gates go red**, including all seven
R11 ceiling gates:

```
FAIL  ONE band at 100 %, Amount 100 %: MSD >= 28 dB              MSD 5.2 dB
FAIL  ONE band at 100 %, Amount 200 %: MSD >= 55 dB              MSD 10.4 dB = a factor of 3 on one band
FAIL  a cut at 100 % is a HOLE: <= -70 dB                        floor -5.0 dB (sine probe)
FAIL  Slant 100 %: the TOP end ALONE beats a whole console       +3.94 dB at 8 kHz, +3.99 dB at 16 kHz
FAIL  Slant 100 %: the LOW end ALONE beats a whole console       -3.89 dB at 80 Hz, -3.98 dB at 30 Hz
FAIL  Slant 100 % / Amount 200 %: 20 Hz - 20 kHz spread >= 55 dB spread 16.0 dB (+8.0 top, -8.0 bottom)
FAIL  all four bands at 100 % with Amount 200 %: boost >= 55 dB  peak +34.0 dB, deepest hole -10.3 dB
FAIL  44.1 kHz: the R11 ceiling still reaches 55 dB              MSD 10.4 dB
FAIL  96.0 kHz: the R11 ceiling still reaches 55 dB              MSD 10.5 dB
FAIL  Slant  LOW  end (80 Hz alone — must FALL)                  span 7.8 (need 30)
FAIL  Slant  TOP  end (8 kHz alone — must RISE)                  span 7.9 (need 30)
```

---

## M6 — `EQ_MUT_NO_DENORM`

```
real      ok    a Q 90 Chisel ring decays to TRUE zero (denormal flush)   tail 0.000e+00
mutated   FAIL  a Q 90 Chisel ring decays to TRUE zero (denormal flush)   tail 1.617e-22
```

---

## fb423 — the one baseline FAIL is RULED, and §P is mutated the same way

The fb422 baseline ended **121 pass / 1 FAIL**. The FAIL was §O refusing to pass ten labels
`RENAMES.md` had not ruled on, and refusing to invent replacements (FIXES.md §2: *"Do not
substitute your own"*). The fb423 second ruling settled all ten:

```
fb422   FAIL  no collision with a shipped label outside RENAMES.md's sanctioned list
              18 collisions, 8 sanctioned by name; UNRESOLVED: Stereo (Focus option),
              Mid (Focus option), Forward, Runaway, Program, Stacked, Peak Hold, Razor,
              Telephone, Metal (Characters)

fb423   ok    no collision with a shipped label outside RENAMES.md's sanctioned list
              10 collisions, 10 sanctioned by name; UNRESOLVED: none
        ok    every EQUALIZER row of RENAMES.md is applied VERBATIM      29 of 29 rows
```

8 renamed (`Forward`→`Ahead` · `Runaway`→`Bolt` · `Program`→`Baseline` · `Stacked`→`Twin Shelf` ·
`Peak Hold`→`Peak Keep` · `Razor`→`Scalpel` · `Telephone`→`Handset` · `Metal`→`Tin`), 2 sanctioned
(`Stereo`, `Mid` — M/S routing vocabulary, ruled as a group with `Side`/`Left`/`Right`).

---

## §P — the DOWNSTREAM gates, and their negative controls

> fb425 added **two more**, C1 and C2, documented at the top of this file: the ROSTER's §3
> assignment gate mutated by swapping two Characters between two Types, and the retired-name
> scan mutated by putting the cert's own two fb424 strings back. Both go RED; P5's presence test
> stays green on the same corrupted file, which is the finding.

§P is new at fb423 and gates the two files the card is actually built from — `ROSTER.md` and
`eq-worklet.js` — against the engine header. It is mutated by **breaking the subject files**, not
the engine, because that is where its faults live. All four controls are reproducible: copy the
directory to `/tmp`, break one thing, rebuild the cert against that copy.

| # | control | gates that go RED |
|---|---|---|
| 1 | restore the **fb422 downstream files** (stale worklet + stale roster, header correct — the exact state the fb423 audit found) | **6 of 8**: EQUAL 79/87 · authored-once 43/87 · CSPEC-zero-strings 112 · retired-strings **47 live** · Trait table 2/7 · all-87-present 79/87 |
| 2 | hide **one** retired name inside a LATER blockquote (Max's own §0 quote) | retired-strings: `Metal ROSTER.md x1` — proves the changelog exemption really is banner-only |
| 3 | **delete `ROSTER.md`** | readable: `ROSTER.md NO` — a gate that cannot find its subject goes RED, it does not skip (fb393) |
| 4 | retitle the banner so the exemption is no longer a changelog | exemption: `NOT a changelog heading` |
| 5 | §O: add a **sanction for a name that was never ruled** (`Plain`) and let it collide | the primary collision gate goes **green** (`11 collisions, 11 sanctioned`) — and the meta-gate goes **RED**: `11 of 16 sanctions are actually spent`. This is the control that matters: it proves the exemption list cannot buy itself a pass. |

```
control 1   ══ RESULT: 125 pass, 6 FAIL ══
control 2   ══ RESULT: 130 pass, 1 FAIL ══     FAILED: not one of the 29 RETIRED strings
                                                       survives downstream (prose included)
                                                       [1 retired strings still live:
                                                        Metal ROSTER.md x1
                                                        [> — and the Metal character stays as it is.]]
control 3   ══ RESULT: 123 pass, 1 FAIL ══     <- 123 = every gate through §O, i.e. the
                                                  fb422 baseline's 121/1 resolved to 122/0
control 4   ══ RESULT: 130 pass, 1 FAIL ══
control 5   ══ RESULT: 130 pass, 1 FAIL ══     ok    no collision with a shipped label outside
                                                     RENAMES.md's sanctioned list
                                                     11 collisions, 11 sanctioned; UNRESOLVED: none
                                               FAIL  the sanctioned list SPENDS exactly 10
                                                     exemptions — it cannot quietly grow
                                                     11 of 16 sanctions are actually spent
```

The meter is pinned at **9** as of fb425 (**8** at fb423, before this device published its two
dropdown headings — `Character` is a live collision and is ruled, cited, in `kSanctioned`). It was 10 at fb422; refreshing the corpus against the
siblings' current state returned two exemptions UNSPENT (`Bite`, because OTT renamed its pill
`Bite`→`Crest`; `Silk`, because Widen renamed its Character `Silk`→`Satin`). The same refresh
exposed a **circular corpus** — the siblings' own gate dumps re-export my 87 labels, which briefly
produced `87 collisions ... 14 of 15 sanctions spent` — see `FINDINGS.md` §4. That is a family-wide
defect and only my own extractor is fixed here.

fb423's own words: *"a gate that can exempt itself is not a gate."* Control 5 is that sentence made
executable — the sanctioned list is pinned to the number of collisions it actually **spends** (9
at fb425), so a sanction can never be written ahead of a ruling. The carried-but-unspent entries
(`Bite`, `Reach`, `Focus`, `Silk`, `Side`, `Left`, `Right`) are printed every run: `Side`/`Left`/`Right` exist only
because the M/S vocabulary was ruled **as a group**, and the gate says so out loud rather than
letting them sit silently.

🪤 **The exemption gate earned its place by catching me.** Its first version dropped *every*
markdown blockquote line from the retired-name scan — and immediately failed, because §0 quotes
**Max** in a blockquote. An exemption wide enough to cover Max's own words is a blind spot in the
one section a reader trusts most. It is now exactly one region: the contiguous blockquote that
opens the file, asserted contiguous, asserted to end before the first `##` heading, and asserted
to *begin with a changelog heading* — so it cannot quietly become "exempt prose". Its size is
printed every run (currently 96 lines).

The six engine mutants leave §P **green in all six** (0 of 8 P gates fire), which is correct and
worth stating: §P measures files, not DSP. It is falsified by controls 1–4 above, not by `-D`.
