# bod_cert — MUTATION LOG (fb444)

fb421: **a gate that has never failed has never been tested.** Every gate below was
either (a) demonstrably red on a real defect during development, or (b) deliberately
broken afterwards to confirm it notices. Anything that could not be made to fail is
recorded as such, honestly, rather than counted as coverage.

## Gates that went red on REAL defects, then green on the fix
| Gate | The defect it caught | Evidence |
|---|---|---|
| §A gate 0 — direction | The sideband sign was inverted: `m = -(2b-1)`. dir=up put a 1 kHz tone at **899 Hz**. A working shifter running backwards — it passes every level, click and "spectrum changed" gate. This is the same defect the build bible's own first draft shipped. | `peak 899 Hz · 1100 is -49.0 dB over 900` → after fix `peak 1100 Hz · +49.0 dB` |
| §B — range | Consequence of the above: full travel landed 1 kHz at **4000 Hz** (1000 − 5000 folding through zero) while the engine meter correctly read +4999.8 Hz. The meter is what proved it was the sign and not the mapping. | `-> 4000 Hz (want 6000)` → after fix `-> 6000 Hz` |
| §F — Blur | Blur was `floor(blur * 4)` allpasses, so knob 0 and knob 1/6 both resolved to ZERO sections: the first sixth of the travel was **dead**, exactly 0.000 dB apart. Now a continuous crossfade with all four sections always warm. | `smallest spectral step = 0.000 dB` → `1.063 dB` |
| §F — Damping | Range was 700 Hz…**40 kHz**, and the LP clamps at 0.45·fs, so the top third of the knob was one repeated filter. Now 400 Hz…20 kHz, every position live. | `0.070 dB` → `0.382 dB` |

## Gates broken deliberately afterwards
| Mutation | Gates red | Caught by the target gate |
|---|---|---|
| delete the input-presence gate | 8 | YES — free-run |
| delete the stereo shift mirror | 2 | YES — Spread |
| delete Low Keep's dry bypass | 1 | YES — Low Keep |
| move the makeup OUTSIDE the loop | 1 | YES — unit slope at zero |
| delete the in-loop soft clip | 1 | YES — **after the gate was fixed twice, see below** |

## The clip gate, which took three tries — and what it taught
1. **First form: "does it diverge?"** Mutation → **0 red**. The question can never fail:
   at the then-ceiling of `kFbMax = 0.95`, with Blur allpass (unity) and Damping
   cut-only, the loop is *provably* BIBO-stable with no guard at all.
2. **The ceiling was the timidity.** A maximum that cannot misbehave is not a maximum.
   Raised to **0.995** (200× peak), matching the fx3 arc that took the phaser to 0.998.
3. **Second form: "hot input, is the output bounded?"** Still **0 red** — because the
   gate pinned `blur = 0.4`, and diffusion spreads the loop's energy in TIME. At blur
   0.4 an unclipped loop peaks at 1.95 and slips under a 2.0 bar; the SAME loop at
   blur 0 reaches 9.4. **One arbitrary knob position hid a 14 dB defect.**
4. **Third form: sweep the matrix** (fb425 — 8 chars × 3 blur × 3 time = 72 cells,
   report the worst CELL, not an average). Mutation → **RED at 90.84 (+39.2 dBFS)**,
   worst cell `blur 1.0 / time 0.20` — a cell the sampled version never visited.
   Shipped engine, same sweep: **1.45 (+3.2 dBFS)**.

## NOT load-bearing — recorded rather than claimed
| Mechanism | Mutation result | The honest reading |
|---|---|---|
| squaring the gate release (`gate_ * gate_`) | **0 red**, and direct measurement of the release envelope shows −40.3 dBFS (squared) vs −39.7 dBFS (linear) at +400 ms — **0.6 dB apart** | The gate's own release is already fast relative to the loop decay, so the square changes nothing measurable *in this engine*. It is kept because it matches the flanger idiom and becomes load-bearing the moment anyone lengthens the release — but it is **not** covered by a gate, and this table says so instead of counting it. |
