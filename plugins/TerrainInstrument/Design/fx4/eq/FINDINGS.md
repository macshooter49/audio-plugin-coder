# EQUALIZER — FINDINGS (fx4, chain kind 9)

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

### 2.2 The ±48 dB tilt generated its own noise floor, and the click gate blamed the sweep

The 1-pole shelf was designed as `m²(1+K/m)/(1+Km)`. Algebraically correct; numerically lethal.
A ±48 dB tilt needs a 96 dB shelf, whose `b0` and `b1` then both sit near 5000 and differ by
one part in 20 000 — a `float` direct form loses the entire low end to cancellation. The click
gate reported a **27.5 dB frame jump on Tilt**, which was not a click at all: it was arithmetic
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

**Consequence, and it is a real behavioural change worth telling Max about:** British `Bump` and
Surgical `Width` progressively **stop resonating the AIR band** as `Reach` climbs past ~0.35·fs.
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
| §5.1 "Tilt ±12 dB" | Too polite for R11 | **±24 dB**, ×Amount = ±48 |
| §2.1 "seven Types, ±18 dB, Sculpt ×1.67" | — | **±30 dB on every band**, per contract R11. The Type-specific gain scaling became a *law* (British softener, Open tanh knee, Passive deeper atten, Sculpt notch morph) rather than a multiplier |

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

`Dynamic` → `Sense` sweeps a **threshold**, and against a fixed-level probe a threshold
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
   bounces ±4 dB with *nothing moving*. The gate reported an 11.03 dB "click" on Tilt at
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
