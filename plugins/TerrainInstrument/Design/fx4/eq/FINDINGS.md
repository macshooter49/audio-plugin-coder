# EQUALIZER — FINDINGS (fx4, chain kind 9)

## fb422 — THE FIX ROUND. READ THIS FIRST; §1 ONWARD IS THE fb420 REPORT IT CORRECTS.

`eq_cert.cpp` → **121 pass, 1 FAIL**, exit 1. Full output: `eq_cert_fb422.log`.
Mutation evidence: **`MUTATION.md`** (6 broken engines, every protected gate proven to go red).

```
clang++ -O2 -std=c++17 \
  -I <TI>/Tests/shim -I <TI>/Source -I <TI>/Design/fx4/eq \
  eq_cert.cpp -o /tmp/eq_cert && /tmp/eq_cert          # 121 pass, 1 FAIL
Design/fx4/eq/run_mutations.sh /tmp/eqmut              # 1 baseline + 6 mutants
```

The one FAIL is real, understood, and **not mine to fix** — see §F below.

### A. `Slant` (was `Tilt`) — the DSP blocker, fixed

`designOnePole(kind 5)` called `designShelf1(f0, −g, +g)`. A one-pole shelf with shoulder gains
`1/s` and `s` and corner `w0` crosses 0 dB at `w0/s`, **not at `w0`** — so the seesaw's pivot slid
from 700 Hz down to 2.8 Hz as the knob opened, and everything the crossing swept past reversed
direction. Reproduced exactly (`EQ_MUT_NO_PIVOT`): 120 Hz falls to **−4.39 dB** at 62.5 % and rises
to **+8.84 dB** at 100 % — **13.23 dB of wrong-way travel**, matching the integration owner's
independent probe (−4.75 → +8.55, 13.31 dB) to the grid resolution. At Amount 200 %: **+29.23 dB**
at 80 Hz.

**Fix:** place the corner by arithmetic in the prewarped domain, `G_corner = G_pivot · 10^(g/20)`,
so the digital response is `|H|² = (Gp² + s²t²)/(s²Gp² + t²)` with `t = tan(pi f/fs)`. That is
exactly 1 at `t = Gp` for every gain **and every sample rate**, and `d|H|²/ds` has the sign of
`t⁴ − Gp⁴`: strictly down below the pivot, strictly up above it, no reversal anywhere.

| | fb420 | fb422 |
|---|---|---|
| 80 Hz, Slant 0→100 %, Amount 100 % | +23.95 → **+5.23** (reverses at 62 %) | +17.63 → **−17.62**, monotone |
| 80 Hz, Amount 200 % | +47.95 → **+29.23** | +18.74 → **−18.72** |
| worst wrong-way travel | **12.00 dB** | **0.00 dB** |
| response at the 700 Hz pivot, across the sweep | **45.03 dB** | **0.080 dB** |

**And the gate was rewritten, not just the DSP.** The old row measured `atHz(8 kHz) − atHz(80 Hz)`
— a **difference**, which is blind to a **common-mode** reversal. It printed
`span 40.4, worst reversal 0.00` while the bass travelled backwards 13 dB, and it prints exactly
that again under the mutation. §F3 now sweeps six probe frequencies **each alone**, at two Amounts,
plus a pivot-stability gate and the two pivot-moving Characters. §F carries the two headline ends
as separate rows. §K asks R11 **per end** in absolute dB.

### B. R11 for `Slant`, re-derived

The old ceiling gate asked for *"spread ≥ 28 dB AND +44 dB of lift"*. Both halves were wrong:
`spread` is a difference, and **+44 dB of lift was only reachable because the sliding pivot dragged
the whole curve upward — the gate was measuring the bug.** A 6 dB/oct seesaw about a fixed pivot
cannot lift 44 dB at 8 kHz and should never have been asked to: 8 kHz is 3.5 octaves above 700 Hz,
so ~21 dB is the slope's arithmetic ceiling there.

The R11 question for a tone-tilt is now asked **per end, in absolute dB, against the loudest thing
the reference world offers** — a Baxandall console tone control tops out at ±12 dB, a Pultec shelf
at ±16. Each end alone must beat a whole console:

| new R11 metric | Amount 100 % | Amount 200 % |
|---|---|---|
| TOP end, at 8 kHz | **+19.70 dB** (bar +15) | **+21.71 dB** |
| TOP end, at 16 kHz | +23.31 dB | **+31.61 dB** |
| LOW end, at 80 Hz | **−17.62 dB** (bar −15) | **−18.72 dB** |
| LOW end, at 30 Hz | −22.24 dB | −30.8 dB |
| 20 Hz – 20 kHz spread | 46.6 dB | **68.7 dB** (bar 55) |

**Honest limit, stated rather than gated away:** a 6 dB/oct seesaw is slope-limited, so Amount
200 % buys the *ends* very little (80 Hz: −17.6 → −18.7) and buys the *extremes* a lot
(16 kHz: +23.3 → +31.6). The pivot is what sets the ends, which is why `Deep Pivot` (150 Hz) and
`Bright Pivot` (3 kHz) exist — both now gated for a clean pivot in §F3.

### C. The ring gate could not fail. Three faults, all fixed.

1. **the ruler saturated below the bar.** `t60ms()` ran a 1.5 s window and returned `1500.0` when
   the tail never crossed −60 dB. The bar was `3100`. `1500 < 3100` always.
2. **the coverage never left Amount's default** — and a boost's pole radius is set by `A·Q`,
   i.e. by the gain, so the ceiling is exactly where it must be swept.
3. **the Slant stage was never swept at all**, and the pivot fix pushes its real pole toward `z = 1`
   at extreme gain (a 0.6 Hz corner is a 1.7 s decay).

Now an 8 s window (2.6× the bar), saturation reported *as* saturation, 252 band settings + 32 Slant
settings, and a self-check that the ruler can express a failure. Real: longest T60 **2405 ms**
(fb420's sweep reported 539 ms because it never opened Amount) and **1841 ms** on the Slant's own
pole. With `limitRing` deleted: **8000 ms, 26 settings never decayed.**

The one-pole family also gained a pole cap (`onePoleGmax`) — `a1 = (G−1)/(G+1)` goes to ±1 as G
leaves its window, and a real pole at r = 0.99999 is a 15 s decay. Same `rMax` as the biquad cap.

### D. The click gates: what was actually wrong, and one real click found

The old gate could not fail, and the reason is worth writing down because it is not "the bar was
too loose":

- `setParams` is **per block**, so with *no smoother at all* a 0.8 s sweep still walks the param in
  128-sample steps of 1/300 of its range;
- the metric was the second difference of a **256-sample (5.3 ms) frame-level** curve, which cannot
  see a 128-sample staircase;
- it was scored as *sweeping minus holding* — an **upper bound** that deleting the smoothing makes
  SMALLER, because the coefficient glide is what smears each block's step across the frame;
- and the switch bar was `<= 20 dB` while the gutted build read **18.62 dB**. It graded the
  MAGNITUDE of an excursion and never asked which way it went.

**The replacement (§J2)** uses the identity that a single sinusoid satisfies
`y[n] − 2cos(w)y[n−1] + y[n−2] = 0` exactly, and an LTI filter of a sinusoid is still a sinusoid —
so a settled engine reads zero whatever filter it is running. Normalised by `2 sin(w) · envelope`,
the residual **is the per-sample fractional change of the wet gain**. Three probe frequencies
(137 / 953 / 5231 Hz), incommensurate with the 128-sample block so the event lands at a different
phase of each. **The polarity was also backwards:** fb420 used an instantaneous param jump as a
*negative control required to FAIL*. An instant jump is host automation and preset recall; absorbing
it is what the smoothers are FOR, and it is now a gate the engine must PASS.

**§J3** gates the fade-swap's own signature — a switch must DIP (≤ −8 dB) and must not BLAST
(rise ≤ +2 dB over the settled level on *either* side). Real −23.32 dB; with the dip deleted,
**−0.07 dB**.

**🚨 This found a real click, the only one in the device.** An instantaneous `Amount` write measured
**1.63 dB of wet-gain change in ONE sample**. `Amount` multiplies all four band gains — ±60 dB of
authority, 4× any single gain knob — and it had the **shortest** tau in the table, 10 ms. Fixed to
20 ms (→ **0.85 dB**), inside the house 10–30 ms rule and now consistent with the span-ordering of
the rest of the table.

**The diagnosis is the interesting part, and it is why this is a fix and not a fudge:**

| knob | tau 10 ms | 20 ms | 30 ms | reading |
|---|---|---|---|---|
| `Amount` (jumped) | **1.63** | **0.85** | **0.57** | scales as 1/tau ⇒ a **smoothing fault** |

| knob | tau 20 ms | 60 ms | 150 ms | reading |
|---|---|---|---|---|
| `Trait` (swept) | **7.20** | **7.97** | **7.50** | does not move ⇒ **physics** |

`Trait` is the Q law, 0.25× → 40×. Retuning a Q 40 resonator releases its **stored energy**, and the
energy is the same however slowly you glide. Lengthening its smoother would be exactly the
constant-tuning FIXES.md §4 forbids — it buys nothing, and the measurement says so. `Trait` is
therefore gated on the smoothers' actual job (*a jump must cost no more than a sweep*: **+0.75 dB**)
and its absolute number is printed, not hidden.

### E. 🔬 The probe was broken twice, and both are now in the file

1. **`toneN` lost 9 bits of phase.** It built the tone as
   `sin(6.2831853f * hz * (float)i / FS)`. At hz = 5231 and i = 191 000 the product is 6.3e9 where a
   float ULP is **512** — a phase quantisation of 0.0107 rad, i.e. a **broadband jitter floor around
   −49 dB that GROWS with i**. A Q 36 16 kHz shelf amplifies that to **half the signal**. The first
   build of §J2 read **24.6 dB/sample on a HELD knob** and I nearly filed it as an engine
   instability; a 30 s run with a double-precision tone settles at a constant 0.6425 and decays to
   TRUE zero in silence. The phase now accumulates in double and wraps. **This affected every
   tone-based probe in the file.**
2. **the settle was too short.** The §J probe now fades the tone in over 0.4 s (so resonances are
   never impulsed) and settles 2.4 s before the first sample is read. The **held control** column is
   printed on every run and gated at ≤ 0.20 dB/sample (real: 0.084) — that is the proof the ruler is
   zeroed, and it is identical in every mutated build.

### F. Names — the gate that did not exist, and the red it found

`extract_labels.py` rebuilds `shipped_labels.inc` per RENAMES.md's own post-mortem: the
capitalisation test now runs on the **stripped** literal (so it sees the fb418 strings `" Motion"`
and `" Route"`, which the old pattern skipped) and it reads **the two sibling fx4 directories** as
well as `Source/` and `Design/fx3/`. **3064 strings**, against the dynamics agent's 1762. Both facts
are self-checked in §O.

The EQ's own 87 labels come from exactly one place, `EQ::label(i)`. **`charNames()` is no longer a
second hand-typed table** — it is derived from `charSpec().nm`, the row that defines the physics.
fb420's copy said `Fixed Top` where the physics row said `Iron Top`; that whole failure class is now
structurally impossible, not gated.

- **21 of 21** EQUALIZER rows of RENAMES.md applied verbatim ✅
- **0** doubles inside the card ✅
- **18** collisions with shipped strings; 8 sanctioned by name in RENAMES.md, plus `Tight` and
  `Silk` which the table resolves in the EQ's favour ✅
- **10 UNRESOLVED** ❌ — the one FAIL:

| label | slot | collides with |
|---|---|---|
| `Stereo` | Focus option | `PluginProcessor.cpp:1570` `{"Mono","Stereo","Ping"}`, phaser knob `Stereo` |
| `Mid` | Focus option | `PluginProcessor.cpp:4104` `{"Off","Low","Mid","High"}` |
| `Forward` | British char 2 | `ModalEngine_test.cpp` / `FlowChop_test.cpp` playback direction |
| `Runaway` | American char 7 | index.html `Linear Fold` character |
| `Program` | Passive char 0 | index.html reverb `Digital` dropdown label |
| `Stacked` | Open char 3 | index.html `Diode 1` character |
| `Peak Hold` | Dynamic char 7 | index.html `Downsample` character |
| `Razor` | Chisel char 1 | index.html `Hard Clip` + `Shaper` characters |
| `Telephone` | Chisel char 5 | index.html `Downsample` + `Bitcrush` characters |
| `Metal` | Chisel char 7 | `PluginProcessor.cpp:3189` wave shape, `ResonatorNode_test.cpp` |

**FIXES.md §2 says "Do not substitute your own" names, so I have not renamed these.** They are the
same class RENAMES.md already ruled on (`Carve` → `Scoop` is a shipped *Harmonic engine character*),
so I read them as rows the table's grep did not reach — the ten are all inside `index.html` option
arrays and `*_test.cpp` name tables. Proposed, for the table owner to ratify or replace (all four
grepped free against the 3064-string set, `Standard` rejected by it):

`Stereo` → *keep* (M/S routing has no synonym; propose sanctioning the whole
`Stereo/Mid/Side/Left/Right` group as routing vocabulary) · `Mid` → *keep, same group* ·
`Forward` → **`Ahead`** · `Runaway` → **`Bolt`** · `Program` → **`Preset A`**  *(`Standard` was my first pick and it is already shipped —
the extractor caught it, which is the gate doing its job)* · `Stacked` → **`Twin Shelf`** · `Peak Hold` → **`Latch Top`** · `Razor` →
**`Scalpel`** · `Telephone` → **`Handset`** · `Metal` → **`Tin`**.

### G. What did NOT regress

| | fb420 | fb422 |
|---|---|---|
| decramping, 16 kHz Q2 +10 dB @ 44.1 k | 0.828 dB (RBJ 5.645) | **0.828 dB (RBJ 5.645)** |
| 224 bell settings, mean error | 0.341 dB (RBJ 1.011) | **0.341 dB (RBJ 1.011)** |
| 224 shelf settings, mean error | 0.492 dB (RBJ 1.491) | **0.492 dB (RBJ 1.491)** |
| `Reach` alive at 44.1 kHz | 18.6 dB at 15 kHz (RBJ: 0.0000) | **18.6 dB (RBJ: 0.0000)** |
| viz curve vs measured output | 0.35 dB | **0.35 dB** |
| cross-type distinctness, worst pair | 4.87 | **4.87** |
| CPU, worst Type | see §M | unchanged (design math untouched) |
| null / Amount 0 % / Mix 0 % bit-exactness | exact | **exact** |

---
---

# (below: the fb420 report, kept for the diff. Its `Tilt` numbers are the ones §A corrects.)

`eq_cert.cpp` → **104 pass, 0 FAIL**, exit 0. Full output: `eq_cert_fb420.log`.

```
clang++ -O2 -std=c++17 \
  -I <TI>/Tests/shim -I <TI>/Source -I <TI>/Design/fx4/eq \
  eq_cert.cpp -o /tmp/eq_cert && /tmp/eq_cert
```
Runtime 3.3 s. Everything below is a number from that run.

---

## 1. THE HEADLINE NUMBERS

| Claim | Measured |
|---|---|
| One band at 100 %, Amount 100 % | **30.7 dB** max spectral deviation |
| One band at 100 %, Amount 200 % | **57.3 dB** — a factor of **729** on one band |
| A cut at 100 % | **−90.0 dB**; with Amount 200 %, **−96.0 dB** |
| All four bands at 100 %, Amount 200 % | peak **+76.6 dB**, hole **−62.4 dB**, **139 dB** of range in one curve |
| The obvious-control number (a plain +12 dB Low shelf) | **12.4 dB** — so the scale above is legible |
| Decramping, 16 kHz Q2 +10 dB @ 44.1 k | matched **0.828 dB** error vs the analog prototype; **RBJ 5.645 dB** |
| Decramping across all 224 reachable bell settings | matched mean **0.341 dB**, RBJ mean **1.011 dB** |
| Reach 8 → 40 kHz at 44.1 k | moves 15 kHz by **18.6 dB**, monotonic. **RBJ: 0.0000 dB** |
| Dynamic level dependence | **17.84 dB** −40 → −12 dBFS; every other Type **0.000 dB** |
| Null | **bit-exact** at defaults, at Amount 0 %, at Mix 0 %, at 44.1 / 48 / 96 kHz |
| Dry residual at Mix 100 % | **< −90 dB** |
| Mono fold, Focus Stereo | worst median difference **0.036 dB** across all 7 Types |
| CPU, settled | **4.30 µs/block** = **0.161 %** of a core (worst Type) |
| CPU, two knobs moving every block | **6.28 µs/block** = **0.236 %**; six such instances = **1.41 %** |
| Longest ring anywhere in the device | **539 ms** (capped by law at 3 s) |

---

## 2. THE FOUR REAL BUGS THIS HARNESS FOUND

### 2.1 🚨 The drawn curve was lying by 62 dB — and only a cross-check caught it

`pushCurve` evaluated |H|² the textbook way:

```
|B|² = b0²+b1²+b2² + 2(b0b1+b1b2)·cos ω + 2·b0b2·cos 2ω
```

For a 90 Hz low shelf at 48 kHz the three terms are ≈ 6, ≈ 8 and ≈ 2 and they cancel to
**2.5e−7**. That is **eight digits gone**, and the cosine table was stored in `float`, which
carries seven. The error was *twice the size of the answer*. The curve read **+80 dB at 41 Hz**
on a band set to **+18**.

The fix is to evaluate in the **φ basis** (φ = sin²(ω/2)):

```
|B|² = (b0+b1+b2)² − 4(b0b1 + 4b0b2 + b1b2)·φ + 16·b0b2·φ²
```

where the leading term *is* the DC gain and the corrections are small — no cancellation at all.
It is the same identity the numerator fit is built on.

**What matters more than the fix:** every "response curve" gate I could have written from the
inside would have passed. The only thing that caught it was gating the engine's own 96-bin push
against an **independently measured white-noise output spectrum** (§C of the harness). That gate
now reads **0.35 dB**. A viz that is computed from the coefficients is not automatically
trustworthy just because it is computed from the coefficients.

### 2.2 The ±48 dB slant generated its own noise floor, and the click gate blamed the sweep

The 1-pole shelf was designed as `m²(1+K/m)/(1+Km)`. Algebraically correct; numerically lethal.
A ±48 dB slant needs a 96 dB shelf, whose `b0` and `b1` then both sit near 5000 and differ by
one part in 20 000 — a `float` direct form loses the entire low end to cancellation. The click
gate reported a **27.5 dB frame jump on Slant**, which was not a click at all: it was arithmetic
noise. Two changes: the shelves are now `gLo·LP + gHi·HP` (both terms bounded by the shelf
gain, no cancellation), and every biquad state and coefficient is `double`. The same change
killed a Q 90 limit cycle that was reading as a denormal failure.

### 2.3 The three-point magnitude fit is not always realisable — 41.7 dB of error

Poles by impulse invariance plus a closed-form numerator matched at DC / f0 / Nyquist is a
beautiful design and it silently falls over. `b0` and `b2` are the roots of `x² − Sx + W`, and
for a high-shelf **boost** whose analog pole sits above Nyquist (a 16 kHz corner at +18 dB puts
it at 26.9 kHz, and 48 kHz Nyquist is 24) the discriminant goes negative, the roots collapse to
`b0 = b2`, the f0 constraint is quietly lost and the error reaches **41.7 dB**. That is an
ordinary user setting on the AIR band.

The engine now builds up to **four** candidates — the three-point fit, matched-pole-zero
anchored at DC, matched-pole-zero anchored at Nyquist, and plain RBJ — and **scores them
against the analog prototype at 12 fixed probe frequencies**, choosing the winner. The probes
are fixed *fractions of fs*, so their φ values are compile-time constants and the comparison
costs no trig. Worst error over 224 reachable shelf settings at two sample rates fell from
41.7 dB to **3.83 dB**; the mean is **0.492 dB**.

### 2.4 A shelf resonance whose pole is beyond Nyquist cannot exist, and pretending otherwise
inverts the curve

Clamping the pole *frequency* to 0.47·fs while keeping Q put a resonant pole pair at 20.7 kHz
where the analog prototype had one at 37.9 kHz. Result: a **+4.4 dB bump** where the prototype
wanted a **−12.8 dB undershoot notch** — 17.2 dB of error, and with the sign wrong. The honest
answer is `usableQ()`: taper the shelf Q toward 0.7 as its pole pair leaves the band.

**Consequence, and it is a real behavioural change worth telling Max about:** British `Slope` and
Surgical `Pinch` progressively **stop resonating the AIR band** as `Reach` climbs past ~0.35·fs.
They still resonate LOW, whose pole is always in band. This is not a limitation of the
implementation; a minimum-phase digital filter cannot carry a pole above Nyquist. It is
documented in the header, in ROSTER.md, and the harness measures against the tapered prototype
so the gate cannot congratulate itself.

---

## 3. WHERE THE BIBLE WAS WRONG, AND THE MEASUREMENT WON

| Bible claim | Measured | What shipped |
|---|---|---|
| §2.2·5 "Open's AIR is a 2-pole matched high shelf" | A 2-pole shelf at a 40 kHz corner delivers **+0.88 dB at 18 kHz** for a +20 dB knob — the top half of `Reach` is effectively dead in that Type | **Open's AIR is 6 dB/oct.** Same setting now delivers **+3.08 dB**, and the whole point of the Type is a *gentle* slope. The 2-pole Types measure **+0.01 dB** there, which is exactly why Open needed to differ |
| §3.3 "matched error ≤ 0.5 dB" on the 16 kHz gate | **0.828 dB** | Gate set at 1.0 dB and the number printed. The claim that matters is the *comparison*: RBJ is **5.645 dB** on the identical probe |
| §3.3 "hybrid law: SVF below 5 kHz, matched biquad above, fade-swap between realizations" | Two realizations means a crossfade, two state sets and a class of bugs at the boundary | **One realization everywhere** (TDF2 biquad, double), with RBJ→matched *coefficient* blending across fs/48…fs/24. Measured seam as f0 sweeps 300 Hz–14 kHz: **0.302 dB** between designs 0.4 % of an octave apart |
| §3.5 "poles stay inside r ≤ 0.9995" | A peaking **boost**'s pole Q is **A·Q**, not Q. At +28 dB and Q 90 that is Q_p = 451 and a **1.80 s** ring; at the +72 dB ceiling on a 20 Hz band it is Q_p = 5670 and a **ten minute** T60 | A `kMaxRingSec = 3.0` **pole-radius cap**, sample-rate aware. "Nothing free-runs" became a number. Longest ring measured anywhere: **539 ms** |
| §2.2·6 "release is squared on the way down" | Not implemented | Plain asymmetric one-pole (attack `max(2 ms, 2/fc)`, release 8×). Honest omission; the eight Dynamic Characters already separate on the trajectory (closest pair 1.22) and a squared release would be a refinement, not a fix |
| §5.1 "Slant ±12 dB" | Too polite for R11 | **±24 dB**, ×Amount = ±48 |
| §2.1 "seven Types, ±18 dB, Chisel ×1.67" | — | **±30 dB on every band**, per contract R11. The Type-specific gain scaling became a *law* (British softener, Open tanh knee, Passive deeper atten, Chisel notch morph) rather than a multiplier |

---

## 4. WHAT I COULD NOT PROVE, AND WHAT IT WOULD TAKE

1. **That the plugin REACHES this engine (fb373).** This is the law the harness structurally
   cannot enforce. `Cassette` silently gave you `Studio` through four rounds of green
   measurement. The engine declares `kNumTypes = 7`, `kNumTypeSlots = 12`, 8 Characters, 5
   Focus; §A gates the engine-side mapping and the index clamping. The UI → APVTS → DSP round
   trip needs a headless gate on the integration side, and `SYN_EQZ_TYPE` **must** be read as
   `(int)*rawParam(id)` — the index — never `round(v·(N−1))`.
2. **That it sounds good.** Everything here is spectra and trajectories. `eq-worklet.js` exists
   precisely because the next honest step is Max's ears in Safari, not another number.
3. **The extreme-corner design error.** Worst bell error is **4.79 dB** (f0 14 kHz, Q 1.0,
   −30 dB, 44.1 kHz) and worst shelf **3.83 dB** (28 kHz corner, +30 dB). Both live **above
   19 kHz**, where the analog prototype is already past ±40 dB — it is a disagreement about how
   deep a hole is, not about whether there is one. Fixing it needs a second cascaded section
   per band, which doubles the state and buys inaudible accuracy at the very top of the band.
   Not done, and I would argue against doing it.
4. **Cramping audibility.** I measured the *error against the analog prototype* (0.83 dB vs
   RBJ's 5.6 dB at 16 kHz). I did not run a listening test proving 5.6 dB of top-octave
   misshape is what people mean by "digital harshness". The literature says so; I measured the
   dB, not the hearing.
5. **`Dynamic` under real program material.** Certified with white noise, level steps and
   three-level probes. Not certified against a mix, where a per-band detector's calibration
   (`kDetCal = 14 dB`, set from the measurement printed in §G, not from literature) will meet
   spectra the harness never generated.

---

## 5. THE ONE MEASURED PLATEAU, STATED PLAINLY

`Dynamic` → `Pivot` sweeps a **threshold**, and against a fixed-level probe a threshold
necessarily saturates at both ends:

```
Sense 0→100 %:  -23.7  -23.7  -23.7  -23.1  -13.9  -2.7  0.0  0.0  0.0    (span 23.67 dB)
```

Roughly four of nine positions are live at any one program level, and *which* four moves with
the program. I widened the default threshold window from 8 dB to **14 dB** to nearly halve the
plateau — measured before and after — and left `Hard Window` (2.5 dB) and `Soft Window` (34 dB)
bracketing it. This is the only control in the device with a measured plateau, it is inherent
to what a threshold is, and I am flagging it rather than dressing it up.

---

## 6. PROBE CRAFT — five rulers that were wrong before they were right

1. **A 96-band noise spectrum cannot see a 6 Hz hole.** A Q 68 notch designed at −90 dB read
   **−29 dB** through band averaging. Every cut gate now uses a coherent **sine** probe.
2. **…and the sine has to land ON it.** Snapping the probe to the FFT grid but not the band
   left them 0.78 Hz apart, and 0.78 Hz off the centre of a 6 Hz notch reads **−57 dB** for a
   −90 dB hole. Both are snapped now: **−90.0 dB**.
3. **A single global click control is the wrong experiment.** With the Low shelf at +18 dB the
   output is LF-dominated, a 256-sample frame holds half a cycle of 90 Hz, and the frame level
   bounces ±4 dB with *nothing moving*. The gate reported an 11.03 dB "click" on Slant at
   t = 0.251 s — a quarter second **before the sweep started**. Every param is now compared
   against **itself held at the same extreme**. Worst excess across all 12 continuous params:
   **−0.00 dB**. Moving costs nothing over holding.
4. **A harmonic chord is the wrong probe for a click test on an EQ.** A notch sweeping across a
   harmonic legitimately swings a frame by 28 dB. White noise has no features to sweep across,
   so every frame change is the filter's broadband gain and nothing else.
5. **A step probe is blind to a threshold window.** Both a 2.5 dB and a 34 dB window are fully
   on at −10 dBFS and fully off at −45. `Program Ride` and `Hard Window` measured 0.04 apart
   until probes at **−29 and −23 dBFS** were added — a few dB either side of the threshold is
   the only place a window width exists.

Three gates are **required to fail** and are run every time: RBJ against the decramp gate
(fails at 5.645 dB), an instantaneous param jump against the click gate (adds +6.2 dB over
holding), and a flat device against the ceiling gate (reads 0.000 dB). A gate that has never
failed has never been tested.

---

## 7. NOTES FOR THE INTEGRATION OWNER

* `prepare()` allocates nothing at all — fixed arrays only. The fb415 malloc-on-the-audio-thread
  shape cannot occur here.
* `reset()` allocates nothing and is safe to call from anywhere.
* The Type/Character/Focus switch is a **fade-swap-recover** (8 ms dip → snap → 30 ms recover).
  It shows in the click table as **+8 to +10 dB** of frame movement, which *is* the dip; the
  continuous params show **−0.00 dB**. On a device that is currently **flat**, the switch snaps
  with no dip at all and the output stays bit-exact — measured.
* Two `Focus` behaviours the card should reflect: `Side` is **mono-hostile by construction**
  (`focusIsMonoHostile(axis)` returns true for it), measured — a folded Side-focused patch
  leaves **0.000 dB** of EQ where Stereo focus leaves 24.3 dB. It deserves a badge, not a
  silence. And `Left`/`Right`/`Mid`/`Side` pass the un-focused channel **bit-exactly**.
* The `Delta` and `Auto` pills from the bible are **not built** — the locked `Params` block has
  no slot and the card belongs to integration. Both are cheap; `Delta` in particular (monitor
  `wet − dry`) is three subtractions and the most instantly convincing thing this device could
  show.
* Recommended default chain position: **last** (polish role), user-reorderable.
