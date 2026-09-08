# bod_cert — MUTATION LOG (fb444 · fb445/446)

fb421: **a gate that has never failed has never been tested.** Every gate below was
either (a) demonstrably red on a real defect during development, or (b) deliberately
broken afterwards to confirm it notices. Anything that could not be made to fail is
recorded as such, honestly, rather than counted as coverage.

**Gate count: 68 pass / 0 fail** (fb444 shipped 28). Runtime ~13 s.

---

# fb444 — the original round

## Gates that went red on REAL defects, then green on the fix
| Gate | The defect it caught | Evidence |
|---|---|---|
| §A gate 0 — direction | The sideband sign was inverted: `m = -(2b-1)`. dir=up put a 1 kHz tone at **899 Hz**. A working shifter running backwards — it passes every level, click and "spectrum changed" gate. This is the same defect the build bible's own first draft shipped. | `peak 899 Hz · 1100 is -49.0 dB over 900` → after fix `peak 1100 Hz · +49.0 dB` |
| §B — range | Consequence of the above: full travel landed 1 kHz at **4000 Hz** (1000 − 5000 folding through zero) while the engine meter correctly read +4999.8 Hz. The meter is what proved it was the sign and not the mapping. | `-> 4000 Hz (want 6000)` → after fix `-> 6000 Hz` |
| §F — Blur | Blur was `floor(blur * 4)` allpasses, so knob 0 and knob 1/6 both resolved to ZERO sections: the first sixth of the travel was **dead**, exactly 0.000 dB apart. Now a continuous crossfade with all four sections always warm. | `smallest spectral step = 0.000 dB` → `1.063 dB` |
| §F — Damping | Range was 700 Hz…**40 kHz**, and the LP clamps at 0.45·fs, so the top third of the knob was one repeated filter. Narrowed to 400 Hz…20 kHz. | `0.070 dB` → `0.382 dB` |

## The clip gate, which took three tries — and what it taught
1. **First form: "does it diverge?"** Mutation → **0 red**. The question can never fail:
   at the then-ceiling of `kFbMax = 0.95`, with Blur allpass (unity) and Damping
   cut-only, the loop is *provably* BIBO-stable with no guard at all.
2. **The ceiling was the timidity.** A maximum that cannot misbehave is not a maximum.
   Raised to **0.995** (200× peak), matching the fx3 arc that took the phaser to 0.998.
3. **Second form: "hot input, is the output bounded?"** Still **0 red** — because the
   gate pinned `blur = 0.4`, and diffusion spreads the loop's energy in TIME. At blur
   0.4 an unclipped loop peaks at 1.95 and slips under a 2.0 bar; the SAME loop at blur
   0 reaches 9.4. **One arbitrary knob position hid a 14 dB defect.**
4. **Third form: sweep the matrix** (fb425 — 8 chars × 3 blur × 3 time, report the worst
   CELL, not an average). Mutation → **RED at 90.84 (+39.2 dBFS)**.

---

# fb445/446 — THE EIGHT TYPES ARE EIGHT MACHINES

## The defect the whole round exists for
fb444 shipped `Params::type` as **a field that nothing read**. The processor wrote it,
the engine stored it, and no line of DSP consulted it: all eight Type names ran one
topology. **All 28 fb444 gates were green the entire time**, because every one of them
asks *does the engine work* and none of them asks *does the CONTROL reach a different
engine*. That is fb373 one level up, and §T is the answer to it: 28 pairs, two probes,
a printed matrix, and a signature gate per Type. On the fb444 engine the matrix prints
**0.00 in all 28 cells** (mutation M01 below reproduces exactly that: 14 red).

## Defects this round's cert caught in this round's own engine
| # | What the gate said | The defect |
|---|---|---|
| 1 | `8 Types, 6 distinct Hz at the SAME 75% knob` | Shift/Echobode/Freeze all had a 5000 Hz span and Ring shared Shift's whole delay decade, so three of the eight Types shifted by **exactly the same number of Hz** at the same knob. Spans are now 5000/2500/4000/25/(unipolar)/3000/2000/1500 and no two delay decades coincide. |
| 2 | `Freeze worst of 8 Characters = -65.7 dBFS` | Freeze broke the free-run law by 4 dB at a 360 ms hold. The tail was **not the gate** (which is at 1e-6 by then) — it was the **Niemitalo allpass chain ringing out** after half a second of a loop pinned against the clip ceiling (slowest section τ = 16.6 ms, eight sections in series). The hold has to end early enough to leave the *chain* room to die, not merely the gate. 260 ms hold / 12 ms collapse → **−172 dBFS**. |
| 3 | `First vs Last 0.03 dB` | fb444's routes 4 and 5 were **bit-identical** (same write, same return; the two branches differed only in statement order). The obvious fix — shift the input vs shift the loop's output — measured **0.03 dB apart too**, because *a shifter commutes with a delay up to a constant phase*. Route 5 is now "the shift reaches only what has been DELAYED": dry attack, metallic tail. |
| 4 | `side/mid +0.0 dB on Wide` | Route 3 "Wide" was in the dropdown and implemented nowhere. |
| 5 | `Guard ON vs OFF = +0.0 dB` | `Params::guard` was stored and read nowhere — a pill on the card that did nothing. |
| 6 | `Damp 0.06 … 0.18 dB` on six of eight Types | An in-loop damper does *nothing* at zero feedback and almost nothing at 0.6. Widening its range downward made it **worse** (first step 0.08 dB): "kills the loop" and "kills the loop harder" are the same sound. |
| 7 | `Chorale Damp 0.20 dB` | Chorale was writing the whole three-voice sum back into the loop, smearing the loop's own identity until Damping went dead on that Type alone. |
| 8 | `worst 3.60 (+11.1 dBFS), Guard OFF` | The Hilbert rotation's **envelope can exceed the clipped peak** (the analytic envelope of a clipped signal is bigger than the signal), so the guard-off ceiling was 3× my pen-and-paper bound. Split into two gates with two bars rather than one bar that could hold neither. |
| 9 | (no gate — mutation) | The Barberpole **window** (a 90 Hz in-loop HP) was built, mutation-tested, measured **inert**, and **deleted**. It is a good idea for a *pitch*-shifter barberpole and a wrong one here: an SSB shifter taken below zero **reflects through DC**, it does not pile up on it, so there is nothing at the bottom to fade. |

## Gate problems the mutations found — the METRIC, not the bar
fb417/fb425 both, repeatedly. Five gates were rewritten because they could not fail:

| Gate | Blind form | Why | Fixed form |
|---|---|---|---|
| §T pair matrix | mean \|dB\| **per BIN**, 40 Hz–16 kHz | a pure tone leaves ~16 000 of 16 384 bins holding numerical noise; the mean over a mostly-empty spectrum is mostly the empty part. Echobode vs Detune — a 271 ms echo device vs a 0.9 Hz beater — read **0.91 dB**. | mean \|dB\| **per 1/6-octave BAND** (52 bands). Same pair: **13.47 dB**. |
| §T broadband probe | **white noise** | a frequency shifter is *defined* by what it does to partials; white noise has none, and a shifting loop turns white noise into white noise whatever its topology. Barberpole vs Spiral: **1.53 dB**. | a band-limited **110 Hz sawtooth** — broadband, but built of the one thing this device moves. Same pair: **6.29 dB**. The white-noise number is kept as a printed **negative control**. |
| §G knob travel | per BAND | bands smooth away exactly what Blur (a chain of allpasses) and Time (comb fine structure) move — fb444 already learned this. | per BIN, §F's own instrument, on a noise+tone probe. |
| Guard | broadband spectral distance | a tanh ceiling only touches the PEAKS; the average spectrum barely notices. Read **0.51 dB**. | the peak. Mutation now reads **+15.0 dB**. |
| Freeze capture (T7b) | "how far under the held note does fresh input land" | held and fresh scale **together** — a ratio cancels the mechanism. −7.9 dB with it, −7.8 dB without. | the LEVEL, and its shape: on a capture, driving the loop harder makes it **quieter**. −10.9 dB with it, **+1.8 dB** without. |

## The mutation table (22 mutations, shipped engine)
| # | Mutation | Gates red | Loudest evidence |
|---|---|---|---|
| M01 | the whole Type dispatch (`type_ = 0` — *the fb444 engine*) | **14** | matrix `worst BROADBAND 0.00 · worst TONE 0.00`, 1 distinct Hz |
| M02 | Barberpole's half-step interleave voice | 2 | half-step rung at 800 Hz vanishes |
| M04 | Echobode's wet-is-the-tap | 1 | first "echo" peaks at 30 ms instead of 250 |
| M05 | Detune's carrier bleed | 1 | beat rate 0.90 Hz → 1.80 Hz (ring-mod beating, 2Δ, no carrier) |
| M06 | Ring's sideband clamp (the Direction override) | 1 | sidebands 9.5 dB apart → **49.0 dB** (it became Shift) |
| M07 | Ring's carrier-bleed re-read of Blur | 1 | Blur travel on Ring → **0.00 dB** |
| M08 | Spiral's second, counter-shifted leg | 2 | below/above −7.4 dB → **−47.8 dB** |
| M09 | Chorale's voices 2 and 3 | 1 | 850 Hz partial 132 dB → 20 dB over floor |
| M10 | Freeze's gate HOLD | 3 | hold −1.1 dB → **−92.8 dB** |
| M11 | Freeze's input attenuation | 1 | level vs feedback −10.9 dB → **+1.8 dB** (it piles up) |
| M12 | Freeze's loop-gain FLOOR | 1 | hold at Feedback 0 % → **−180.1 dB** |
| M13 | the per-Type SHIFT spans (all 5000) | 6 | matrix worst TONE 3.97 → **1.66 dB** |
| M14 | the per-Type DELAY ranges (all 0.02–1000 ms) | 3 | Echobode's echo arrives at 30 ms |
| M15 | GUARD | 1 | ON vs OFF +9.8 dB → **+0.0 dB** |
| M16 | the in-loop SOFT CLIP | **5** | Guard-OFF sweep **1 528 062 (+123.7 dBFS)**; all-Types sweep 980.8 |
| M17 | the post-loop EXPANDER (the ceiling) | 1 | Guard ON/OFF spread collapses to +2.2 dB |
| M18 | Damping's outside-the-loop cut | **8** | §F Damping 2.58 → **0.091 dB**, and six §G Types go dead |
| M19 | Damping's second pole | **0** | see below |
| M20 | Route 5's dry-attack topology | 1 | First vs Last 11.24 → **3.22 dB**, i.e. identical to In-loop-vs-First |
| M21 | Route 3, WIDE | 1 | +7.7 dB wider → **+0.0 dB** |
| M22 | Chorale's loop-write discipline | **0** | see below |
| M23 | the aux-line flush | **0** | see below |
| (M03) | Barberpole's in-loop window | **0** | **deleted** rather than shipped — see defect 9 |

## NOT load-bearing — recorded rather than claimed
| Mechanism | Mutation result | The honest reading |
|---|---|---|
| squaring the gate release (`gate_ * gate_`) | **0 red**; direct measurement of the release envelope shows −40.3 dBFS (squared) vs −39.7 dBFS (linear) at +400 ms — **0.6 dB apart** | The gate's own release is already fast relative to the loop decay, so the square changes nothing measurable *in this engine*. Kept because it matches the flanger idiom and becomes load-bearing the moment anyone lengthens the release — but it is **not** covered by a gate, and this table says so instead of counting it. |
| **M19 — Damping's second pole** (LP2 rather than LP1 in the loop) | **0 red** | Since M18 landed, the knob's measurable travel is carried by the **outside-the-loop** cut; the in-loop slope is now a voicing choice, not a mechanism a gate depends on. It costs two multiplies and makes a dark loop genuinely dark, so it stays — but nothing in this file would notice if it went. |
| **M22 — Chorale writing only the PRIMARY voice back into the loop** | **0 red** *(it was 1 red before M18 landed: Chorale's Damping travel was 0.20 dB with the choir recirculating)* | A real fix for a real dead knob that a **later, better** fix made redundant. Kept because a loop that recirculates one clean voice is the correct structure for a parallel voice fan, but its gate coverage evaporated and this table says so. |
| **M23 — flushing the Spiral aux lines while another Type runs** (and the main line while Spiral runs) | **0 red** | Pure state hygiene for **switching Type mid-audio**, which no gate in this file exercises. Without it, Spiral → Shift → Spiral would read seconds-old audio out of a line that went cold. Two writes per sample; kept, uncovered, and recorded. |
| The **post-loop expander's per-Character voicing curve** (the *shape* of `{0,2,6,1,10,40,22,30}`) | M17 zeroes the whole table and turns **1** gate red | That the stage exists is covered (via Guard's peak). That Crush is 40 and Iron is 22 rather than the other way round is **taste**, measured by ear-equivalent reasoning only, and no gate distinguishes them. |
