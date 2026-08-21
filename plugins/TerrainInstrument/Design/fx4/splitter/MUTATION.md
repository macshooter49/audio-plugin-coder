# spl_cert — MUTATION LOG (fb445, the Splitter, kind 14)

fb421: **a gate that has never failed has never been tested.** Every gate in
`Tests/spl_cert.cpp` is either (a) demonstrably red on a real defect during
development, or (b) deliberately broken afterwards to confirm it notices.
Anything that could **not** be made to fail is recorded as NOT load-bearing,
honestly, instead of being counted as coverage.

Harness at ship: **150 gates, 150 pass, 0 fail.**
Compile: `clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/spl_cert.cpp -o spl_cert`
(clean under `-Wall -Wextra`).

---

## 1. Gates that went red on a REAL defect, then green on the fix

Every one of these was found by the harness itself during the build, not by
reading the code.

| # | Gate | The defect | Evidence |
|---|---|---|---|
| D1 | §D mute/solo | **A cert bug, not an engine bug — and it matters that it is recorded.** The solo run overwrote the vectors holding the mute run before the linearity check read them, so the check compared solo+solo. It presented as a hard engine failure across all five Types. | `(mute k)+(solo k)==untouched -0 dB` → after the fix `-153 dB` |
| D2 | §D "mute must remove something" | The gate asked whether muting a lane dropped the **broadband** energy by 3 dB, and went red on a correct engine: on white noise the Low lane of a 500 Hz Low/High split carries ~2 % of the energy, so removing it moves the total by 0.1 dB. **fb417 exactly.** Rewritten to LINEARITY: at Mix 100 % the merge is a plain sum, so `(mute k) + (solo k)` must reconstruct the untouched output. | `smallest mute drop 0.1 dB` (bar 3.0) → `-147 dB` linearity null |
| D3 | §E clamp-repeat | The gate demanded that **every** crossover move for every Split step, which is stricter than the law and red on correct behaviour: once `Span` is at 100 % the TOP crossover sits at the 10 kHz ceiling and stops moving, which is a ceiling doing its job. Narrowed to the crossover `Split` actually names, plus §F on the output. | `0.00 cents` → `17.19 cents` |
| D4 | §F metric, round 1 | Averaged `|ΔdB|` over **linear FFT bins**. 90 % of the bins between 40 Hz and 16 kHz sit above 1.6 kHz, so any control that owns the bottom two octaves averaged to nothing: Split, Lane 1 Width and Lane 1 Pan all measured dead on an engine where they work. Moved to **1/24-octave band averaging**. | Split `0.023 dB` → `1.812 dB` |
| D5 | §F metric, round 2 | The fingerprint was **Mid and Side**, and Mid/Side is **blind to pan**: an equal-power pan preserves `gL² + gR²`, so on decorrelated content both the Mid power and the Side power are invariant under it. Every Pan knob in the device measured dead. Moved to **L and R**. | Top Lane Pan `0.081 dB` → `2.713 dB` |
| D6 | §F probe | Per-lane trims were measured in the full mix, where the bottom half of a fader is masked — a lane 34 dB under its neighbour is inaudible at −60 dB and at −33 dB alike. Per-lane trims are now measured with **their own lane soloed**, which measures the control instead of the masking and still catches a knob wired to the wrong lane. | Left Gain `0.050 dB` → `3.999 dB` |
| D7 | §F, and it changed the ENGINE | With the metric finally honest, `Balance` was found to have a **genuine plateau**: it was cut-only, so its first third moved the losing lane from −60 dB to −40 dB — inaudible at both ends. The winning lane now **lifts +6 dB** as the loser dies (`kBalLift`), so every position of the travel moves the sound and 0.5 is still exactly unity. | Mid/Side Balance `0.013 dB` → `1.981 dB` |
| D8 | §I silence | The window was 0.5 s of silence measured over the last quarter, and the slowest cell (4 lanes × 48 dB/oct, every trim maxed, so the lane is multiplied by +18 dB on the way out) reaches exactly zero at **1.046 s**. The bar stayed **exact zero**; only the window grew, and the number it needed is now printed rather than assumed. | now: `output reaches EXACTLY 0 by 1.064 s` |

---

## 2. Mutations — break the mechanism, confirm the gate notices, restore

17 mutations, applied one at a time to `Source/TerrainSplitterFx.h`, rebuilt and
re-run in full. **15 turn gates red. 2 do not, and §3 says so.**

| # | Mutation | Gates red | Section(s) | The number the gate printed |
|---|---|---|---|---|
| M1 | LR2 high leg **not inverted** (the classic slope-12 trap) | 18 | §A §B §G | `Type 1 × 12 dB recombines -31.47 dB` (bar −100) |
| M2 | delete the **band alignment allpasses** (the `alignLow_` lesson, `TerrainOttFx.h:485-500`) | **28** | §A §B §G | 3- and 4-lane nulls collapse; flatness combs at the upper crossovers |
| M3 | delete the **dry allpass cascade** (dry = raw input) | 27 | §A §B §G | `Type 0 × 12 dB -> +5.88 dB` — the sum is *louder* than the reference |
| M4 | Mix becomes **equal-power** instead of linear | 5 | §B | `worst |ΔdB| across Mix = 3.010 dB at Mix 0.5` — the exact +3 dB an equal-power fade adds when wet == dry |
| M5 | **AP4 loses one of its two BW4 sections** | 12 | §A §B §G | `Type 0 × 48 dB -> -10.06 dB` |
| M6 | `Span` stops **renormalising** against the headroom (fixed 1.4…40) | 3 | §C §E | `lane 2 leakage -0.2 dB down` — the crossovers ran past Nyquist and the lanes stopped being lanes |
| M7 | `Balance` back to **cut-only** (`kBalLift = 0`) | 2 | §F | `Mid/Side Balance smallest step 0.002 dB` |
| M8 | **solo no longer overrides mute** | 6 | §D | `solo-over-mute +316 dB` |
| M9 | only the **first solo** counts (multiple solos stop summing) | 2 | §D | `2 solos +21 dB` |
| M10 | Pan loses its **unity centre** (plain equal-power, −3 dB at centre) | 18 | §A §B | `flat 3.0100 dB` — the whole device 3 dB down at its defaults |
| M11 | Width loses its **unity at 100 %** (`w = 2t + 0.05`) | 1 | §A | `trim chain identity -29.04 dB` — see §4, this one needed a new gate |
| M12 | delete the **crossover glide** | **0** | — | see §3 |
| M13 | delete the **band-output denormal flush** | **0** | — | see §3 |
| M14 | Slope 48 quietly **aliases to Slope 24** | 1 | §C | `6dB -12.1 · 12dB -24.1 · 24dB -48.2 · 48dB -48.2` |
| M15 | the lane fader is **no longer exactly 0 dB at its centre** | 18 | §A §B | `flat 0.3996 dB` |
| M16 | reserved Type slots **fall through** instead of aliasing the default | 1 | §A | `Types 5-7 vs default: -4.9 dB` — see §4 |
| M17 | the **coefficient refresh never runs** after the first block | 1 | §E | `26.4366 dB` — see §4 |

---

## 3. NOT load-bearing — recorded rather than claimed

| Mechanism | Mutation result | The honest reading |
|---|---|---|
| the **15 ms log-domain crossover glide** | **0 red**, and direct measurement says why: peak `|Δy|` across a 5 %→95 % `Split` jump in ONE `setParams` is **0.102 without** the glide and **0.124 with** it — the glide makes the transient marginally *larger*. | A TPT filter's coefficient change is **state-continuous**, so a crossover snap produces no sample discontinuity to detect. The glide is kept because it is the house law for every continuous param and it becomes load-bearing the moment automation arrives coarser than one 64-sample block — but **this harness cannot show that**, and it says so instead of counting it. |
| the **band-output denormal flush** (`lo`/`hi` in `Xover::split`, and `AlignAP::process`) | **0 red.** The output reaches exactly zero at **1.064 s** of silence with the flush and at **1.064 s** without it. | Every state in the cascade is already flushed at 1e-20, so the outputs converge to exact zero on their own. Kept because it makes the split stage's output contract explicit and costs two compares — but no gate covers it. |

---

## 4. The three gates that had to be BUILT because a mutation was invisible

fb421's rule: *if a mutation turns zero gates red, that is a finding about your
GATE.* Three of the four blind mutations were fixed by adding the gate they
proved was missing, not by lowering anything.

1. **M11 (Width off unity) → §A "the default TRIM CHAIN is the exact identity".**
   Nothing in the harness could see it. §A(a) stops before the trims; §B drives a
   **correlated** impulse, which has no Side component for a Width offset to act
   on. The new gate nulls the **merged** output against the phase-matched dry on
   **decorrelated** stereo, across all 32 Type × Slope cells. Ships at
   **−122.33 dB**; the mutation drives it to **−29.04 dB**.

2. **M16 (reserved Types fall through) → §A "reserved Type slots are BIT-IDENTICAL
   to the default".** Falling through made Types 5-7 a 2-lane frequency split,
   which **reconstructs perfectly** and passed everything. That is fb373's exact
   failure mode: a reserved index quietly hands you a different machine and every
   measurement stays green. The bar is bit-identity (−300 dB), not "also works".

3. **M17 (the resolve never runs) → §E "the crossover RESOLVE really runs after
   the first block".** Its own first form **also failed to catch it**, and the
   reason is worth keeping: the gate ran at **unity trims**, and at unity the lane
   sum is an **ALLPASS** — so `|H|` is flat wherever the crossover sits, and a
   frozen crossover is invisible in the magnitude. The crossover only appears once
   the lanes it separates are at different levels. With differentiated lane gains
   the mutation reads **26.44 dB** of error. This is fb350's pool law in
   miniature: delete a per-block resolve and every parameter still *appears* to
   work, because the first block already wrote the coefficients.

The fourth, M12, could not be made to fail by any gate I could construct, and it
is in §3 as not load-bearing.
