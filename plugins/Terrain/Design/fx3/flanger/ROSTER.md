# FLANGER — the locked roster

*fx3, chain kind 7. Engine: `TerrainFlangerFx.h`. Proof: `flanger_cert.cpp` — 83 gates, 0 failures.*
*Every number below is measured by that harness, not asserted. Where a number contradicts the
bible, the number wins and the contradiction is written down in `FINDINGS.md`.*

---

## 0. The one architectural decision, because everything else follows from it

**Every Type is a TWO-DECK machine**: a **reference deck** at a fixed τ0 and a **lag deck** at
τ0 + Δ(t). Wet = `(ref + pol·m·lag) / √(1+m²)`.

A flanger is `dry + delayed`. If the engine returns only the delayed leg then at Mix 100 % — which
house law 3 says is FULLY WET — **the comb vanishes**: 100 % of a 1 ms delayed copy sounds like the
input. Every plugin flanger fixes this by putting a direct leg inside its own wet path, which then
trips "dry residual < −60 dB at Mix 1.0". The two-deck form satisfies both at once, because

```
0.5·(e^{−jωτ0} + e^{−jω(τ0+Δ)})  =  e^{−jωτ0} · 0.5·(1 + e^{−jωΔ})
```

is the classic flanger comb **magnitude** with a pure linear-phase τ0 in front of it. There is
literally no undelayed dry in the wet path, so an impulse at Mix 1.0 reads **y[0] = −142 dB,
measured, exact** (§A). And it is what tape flanging physically *is*: two decks, neither of them dry.

τ0 = **2 samples** for the five short-delay Types (42 µs at 48 k; its own comb against the true dry
at intermediate Mix has its first notch at 12 kHz — inaudible) and **8 ms** for Tape Zero, which
needs the headroom for Δ < 0.

🔑 **The reference deck reads a CLEAN line; the lag deck reads the recirculating one.** If both read
the loop, the transfer becomes `(1 + pol·e)/(1 − g·e)` whose zero and pole land on the *same*
frequency at negative feedback and cancel — the ± Feedback flip then measures as a comb that
**flattens** instead of one whose geography **moves**. This was measured: the first build had ref on
the loop and the polarity gate read **+5.6 dB**; on the clean line it reads **−24.2 dB**, a **62.9 dB
swing**.

**Interpolation: 4-point cubic Hermite (Catmull-Rom), no lower-quality path at any setting.**
Allpass is banned under modulation (its state memory scrambles when the fractional delay moves —
that is a click, not a colour). Linear has a fractional-position-dependent HF droop, so the two
decks stop matching as soon as they sit at different fractional offsets and the through-zero null
shallows at HF. Hermite's own error vanishes identically at frac = 0, and **both τ0 values are exact
integer sample counts**, so the reference deck is interpolation-free and at Δ = 0 the lag deck lands
on the same integer index — the null is a bit-exact cancellation independent of interpolator
quality. Measured artefact floor while sweeping: the click gate (§F) reads **0.5–1.0×** the program
RMS across all 12 knobs at full sweep, against a gate of 12×.

---

## 1. The six Types

| # | Type | Lineage | Mechanism — what is physically different | Measured discriminator |
|---|------|---------|------------------------------------------|------------------------|
| 0 | **Tape Zero** | Itchycoo Park / *Bold as Love* · u-he Satin · Strymon Deco · Arturia BL-20 | Dual deck, ref at 8 ms, lag sweeping **linearly** through Δ = 0 with a zero-dwell exponent that decelerates the crossing. Only Type whose Δ goes negative. | Short-time broadband RMS at the crossing: **−61.2 dB** below the surrounding program (3 ms window). `Add` polarity on the same machine: **−10.5 dB**. Best single-deck comb anywhere in the device: **−21.1 dB**. |
| 1 | **Jet** | MXR M-117 · Boss BF-2 · A/DA | Single swept deck, **exponential** octave sweep (2.66 oct = 40:1 at Depth 100, the A/DA ratio against a 20:1 industry norm), regeneration to ±0.995 with the feedback tap at the comb spacing so the poles land **on** the feedforward series. | Peak-over-median of the wet spectrum: **3.2 → 34.3 dB** across Feedback. Polarity flip moves the emphasised series by half a spacing: **+38.7 dB → −24.2 dB**, a 62.9 dB swing. |
| 2 | **BBD** | EHX Electric Mistress (SAD1024) · Raffel & Smith DAFx-10 | Jet topology inside a bucket brigade: a 4-pole reconstruction filter whose corner **tracks the instantaneous delay** (11 kHz at ≤0.5 ms → 1.5 kHz at 20 ms, because the BBD clock rate goes as 1/delay), plus a compander with mismatched time constants. `Matrix` Character freezes the LFO — the Filter Matrix played by hand. | HF ratio falls **11.4 dB** across the Manual travel where Jet's moves **−2.2 dB** (its recon path is flat) — a 13.6 dB margin. And it is a real dynamics stage: **−2.2 dB** of gain change from −46 to −14 dBFS where Jet moves **0.0 dB**. |
| 3 | **Endless** | Esqueda / Välimäki / Parker, DAFx-15 | Two sawtooth-swept combs 180° apart, each windowed by a raised cosine that is **zero at its own saw reset**, so the crossfade hides every wrap. `Dmin = 0.55·Dmax` is the paper's load-bearing rule; Depth scales the pair, never the ratio. | Comb-trajectory one-signedness **0.96** where every other Type sits at **0.03–0.25**. The notches never reverse. |
| 4 | **Envelope** | A/DA Threshold · Bel BF-20 · BL-20 Env | The sweep's **control source** is the input follower, not a clock. Knee calibrated to the real bus: −38 dBFS floor, −26 dBFS knee, −14 dBFS full sweep. `Rate` scales the follower attack (60 → 1 ms); `Rate` owns BOTH ends of the chase — attack AND release (fb419, when `Tail` became `Drive`). | Correlation of the comb position with the input envelope: **r = 0.99**. Every LFO Type: **r ≤ 0.10**. |
| 5 | **Step** | Subdecay Starlight v2 · trance-gate practice | The sweep target is **sampled**, not swept: a new comb position lands on each tick and **glides** over max(5 ms, 15 % of the step). `Shape` becomes 2–24 quantise steps. | Jump ratio (p93 / median of the comb's frame-to-frame motion): **51.6** vs **20.5** next best. The comb jumps; it does not sweep. |

### Cross-type distinctness matrix (measured, phase-independent)

Fingerprint = 48 log bands of level-invariant magnitude **shape** + 48 bands of temporal
**modulation depth** + 48 bands of **dip depth**, computed on **both L and L−R**, plus a **signed
sweep-direction** term. Distance = worst band, in dB. Sample-difference RMS appears nowhere.

```
             Tape Zero  Jet     BBD     Endless  Envelope  Step
Tape Zero        -      8.0    21.3     20.3     28.4      20.2
Jet             8.0      -     22.1     19.4     25.8      19.5
BBD            21.3    22.1      -      33.0     39.1      41.6
Endless        20.3    19.4    33.0       -      18.4      20.6
Envelope       28.4    25.8    39.1     18.4       -       15.1
Step           20.2    19.5    41.6     20.6     15.1        -
```

**Closest pair 8.0 dB** (Tape Zero / Jet — the two that share the classic voicing, separated by the
through-zero crossing). Gate 4.0 dB. **No Type was cut for blurring into a sibling.**

---

## 2. The Characters — 8 per Type, all 48

Every field a Character re-wires is a **mechanism constant**: deck polarity, sweep excursion, the
delay path's band limit, LFO waveform, the zero-dwell exponent, drift authority, servo damping, the
loop clip knee, the reconstruction corner, compander mismatch, follower attack, base delay, a second
tap's ratio, sweep direction. **Never an output EQ.**

| Type | Characters |
|---|---|
| **Tape Zero** | `Sub` (inverted lag — the full-band null) · `Add` (in-phase doubling, no null) · `Worn Deck` (drift ×2.4, darker path, dwell 1.15) · `Servo` (spring ζ 0.18 — rings on every reversal) · `Wide Zero` (R's sweep inverted — the null crosses the image) · `Deep Zero` (dwell 2.0 — a long stationary hole, span ×1.18) · `Drifting Zero` (the zero POINT itself wanders ±4.5 ms) · `Counter Reel` (the reference deck counter-sweeps — two crossings per cycle) |
| **Jet** | `Silver` (MXR: sine, undamped) · `Compact` (BF-2: triangle, 0.62× damping, shorter base) · `Deep Sweep` (A/DA: span ×1.5, base ×1.2) · `Hollow` (subtractive comb — notches at k/Δ incl. DC) · `Screamer` (loop clip knee 0.22 and driven into it — distorts before it runs away) · `Drop` (ramp LFO — the tape drop) · `Thin Air` (damping ×1.35, base ×0.55 — the comb rides above the bass) · `Twin Jet` (second swept tap at 1.53× — interleaved comb) |
| **BBD** | `Mistress` (the 11 k→1.5 kHz tracking law) · `Deluxe` (corner ×1.6, pump halved, slower) · `Dark Bucket` (corner ×0.4 — dub flange) · `Squash` (compander ×2.1, 4× faster — hard pump) · `Matrix` (LFO ×0.1 — the frozen comb, played by Manual) · `Short Bucket` (SAD512: base ×0.45, corner ×1.75) · `Long Bucket` (MN3010: base ×2.0, corner ×0.62) · `Grind` (subtractive + clip knee 0.40) |
| **Endless** | `Rise` · `Fall` · `Rise Deep` (span ×1.55) · `Fall Deep` · `Double Helix` (L rises, R falls) · `Stacked Rise` (second pair an octave up) · `Soft Rise` (subtractive comb) · `Tight Rise` (span ×0.75, base ×0.6 — dense and fast) |
| **Envelope** | `Up` · `Down` · `Snap` (attack ×0.12, response exponent 2.2) · `Slow Swell` (attack ×6, exponent 0.55) · `Duck Zero` (env drives Δ toward **zero** on two-deck geometry — **loud notes cancel themselves**; nobody ships this) · `Hold` (peak-hold follower — the comb stays where the loudest hit put it) · `Wide Touch` (L/R followers with different attack) · `Deep Touch` (span ×1.7) |
| **Step** | `Random` · `Stair Up` · `Stair Down` · `Pendulum` · `Ratchet` (period ×½,½,1,2 — short-short-long) · `Drunk` (random walk ±1) · `Wide Steps` (R holds the opposite step) · `Glide` (glide 60 % of the step — portamento comb) |

**Measured:** every Character pair inside every Type is distinguishable. Worst pair across all 48
voicings: **1.7 dB** (Jet `Silver` / `Screamer`), gate 1.5 dB. Per-Type closest pairs: Tape Zero 3.4,
Jet 1.7, BBD 3.1, Endless 3.3, Envelope 3.6, Step 5.4 dB.

**Cut on evidence:** the bible's `Endless / Shift` (SSB via a Hilbert quadrature network). It is
literally single-sideband frequency shifting, which is the entire reason the Bode device exists; the
bible's own cross-bible audit flagged it and recommended the cut. Taken. Replaced by `Tight Rise`.

---

## 3. The chassis — 3 heroes + Mix on the front, 8 on the back

### Front

| Knob | Range / taper | What it does |
|---|---|---|
| **Rate** | 0.02–20 Hz log free · synced = the house 20-entry list, 4 bar → 1/256, **identical table and labels in all three fx3 devices** | How fast the sweep moves. Envelope: how fast the comb chases the playing (follower attack 60 → 1 ms). Step: the tick. Measured **0 → 51 sweep reversals**, monotonic. |
| **Depth** | 0–100 → ±2.66 oct (40:1 at max). Endless: notch depth + pair scaling | How far it travels. Measured **1 → 8700 cents** of comb travel, monotonic. |
| **Feedback** | **BIPOLAR, 0.5 = centre.** t^1.5 each side → \|g\| to 0.995 | Resonance. CW = the harmonic-series jet, CCW = the hollow odd series. Measured **3.2 → 34.3 dB** peak-over-median CW and **3.2 → 27.7 dB** CCW, both monotonic. |
| **Mix** | equal-power sin/cos | 100 % = FULLY WET, ZERO DRY. Measured **−142 dB** dry residual (exact, by impulse). |

> ⚠️ **INTEGRATION HAZARD.** `Params::feedback` is **bipolar with 0.5 as the centre**. Wiring a
> unipolar 0-default parameter to it gives **−99 % feedback**, not none.

### Back — 8 knobs, 4×2

| # | Knob | Range / taper | What it does, and why it earns the slot over the alternatives |
|---|---|---|---|
| 1 | **Manual** | 0.1–20 ms, `0.1·200^t`. Tape Zero: **Zero Bias** −7.5…+7.5 ms | The sweep centre, and at Rate ≈ 0 the **playable frozen comb** — the Electric Mistress's Filter Matrix, which is the lesson that the static comb is an instrument. Measured **0.1 → 20.0 ms**, monotonic, ×200 span. On Tape Zero it becomes *where the null sits*, which is the single most expressive control on the flagship. |
| 2 | **Spread** | 0–180° L/R sweep phase offset | The stereo image of the sweep. House-consistent: `Spread` means "the L/R offset of the modulated parameter" in `DelayEngine.h` and `FilterFxEngine.h` already. Measured **−280 → +0.7 dB** L−R, monotonic (mono-identical at 0 by construction). |
| 3 | **Width** | 0–160 % wet M/S, never in the loop | Makes a mono source huge — and is the control that *fixes* mono compatibility by coming back down. Measured **−280 → +2.8 dB** L−R, monotonic. |
| 4 | **Damping** | in-loop LP 20 kHz → 500 Hz **and** delay-path LP 20 kHz → 1.2 kHz, one knob, two corners | 🔑 **This replaces the bible's `Tone`.** A flanger's HF loss happens in two places — once per pass through the delay line, and again on every recirculation, where it compounds. It is the single knob that decides whether high regeneration **sings** or **fizzes**, and on drums it is the first thing you reach for. `Tone` was an output tilt: an EQ the downstream Terrain-Patcher already covers, and CLAUDE.md §5 explicitly forbids bloating a device with those. Measured: the comb's 4–15 kHz teeth collapse **20.8 → 5.6 dB** while the 0.2–0.9 kHz comb survives at **27.9 → 24.4 dB** — a physics change, not a tilt. |
| 5 | **Shape** | LFO **triangle → sine → ramp**. Step: 2–24 steps. Envelope: response curve | The waveform of the motion. The ramp is the tape drop (it returns over 8 % of the cycle — a hard saw reset on a *delay read position* is a click). Order matters: sine→tri→ramp folds back on itself on every shape statistic, which is a law-1 plateau in the middle of the knob by construction. Measured **1.9 → 11.0** sweep-speed crest, monotonic. |
| 6 | **Bounce** | 0–100, servo spring + the SmoothRandom drift stack | 🔑 The Eventide FL-201's own control: the capstan servo overshoots and rings on every sweep reversal, which is *why tape flanging sounds liquid and every naive DSP flanger sounds like a sewing machine*. Named `Bounce`, not `Wobble`, because the shipped Tape device already owns `Wobble` — and because Bounce is the flanger's own word. Measured: the sweep trajectory departs from the un-bounced path by **0 → 57.8 %** of its own excursion, monotonic, of which **0.1 → 8.5 %** is genuinely non-repeating drift. |
| 7 | **Drive** *(fb419 — was Tail)* | in-loop saturation, `g = 1 + 7t²`, clean → +18 dB, with the makeup **inside** the loop | 🔑 The one place a nonlinearity belongs on this device: it is what makes ±0.995 regeneration **growl** instead of merely ring. `tanh(x·g)/g` has unit slope at zero, so `sat'(0)·makeup == 1` and Drive can **never** move the loop gain past what Feedback asked for — otherwise it would be a second, secret feedback control and the 60 s stability gate would be measuring a lie. Measured: HF in the loop **−61.3 → −47.4 dB** while the decay time holds at **1.1 s at every setting**, and **0.00 → 36.96 dB** of spectral change with a note HELD. <br>⚠️ **It replaced `Tail`, which was not dead but CORNERED**: Tail was the feedback GATE's release, so it did nothing at all while a note was held (0.00 dB from 0→100) and a flanger loop is not a reverb — 2 ms at 0.6 loop gain is 30 dB down in ~30 ms. Its whole useful range was one corner, max feedback after note-off, which is not where anyone plays. The gate release is now FIXED at 400 ms (the runaway still dies, still gated) and the Envelope Type's follower release moved to **Rate**, which already owned its attack. ⚠️ The param ID is still `SYN_FLA_TAIL` — an ID is a saved patch's only handle. |
| 8 | **Low Cut** | 20 Hz – 1 kHz, `20·50^t`, **in-loop and wet** | Eventide put Low Cut on the Instant Flanger because flanged bass wobbles the mix floor; in-loop placement also stops sub-bass regen buildup, which is what lets Feedback go to 0.995 at all. Measured **−13.6 → −32.1 dB** LF ratio, monotonic. |

**Rejected for these eight, and why:** `Tone` (output tilt — the Patcher's job, and it changes EQ
where `Damping` changes physics) · `Offset` (a static L/R delay offset — `Spread` already owns the
stereo axis and does more) · `Drive` (the rack has a Distortion device) · a `Quality`/oversampling
control (the device is linear-time-varying except a soft clip that only engages in deliberate
self-oscillation; oversampling would be pure CPU waste and there is no Nyquist-approaching content
that was not already there).

---

## 4. What the harness proves, in one table

| Law | Gate | Measured |
|---|---|---|
| 3 — Mix 100 % = fully wet | dry residual < −60 dB | **−142.4 dB**, exact (impulse y[0]), worst of 6 Types |
| 3 — Mix 0 is transparent | bit-identical | **0.000e+00** worst sample delta, all 6 Types |
| 1 — night and day | 12 params × monotone + span | all 12 monotonic, spans in the table above |
| 2 — Types differ | every pair distinguishable | closest **8.0 dB**, gate 4.0 |
| 4 — Characters change physics | every pair inside every Type | worst **1.7 dB** of 48 voicings, gate 1.5 |
| 4 — no clicks | peak step / program RMS < 12 | worst **1.0×** across 12 knobs × 6 Types at full sweep |
| 4 — no clicks on a swap | < +3 dB | Type swap **+0.67 dB**, Character swap **−0.94 dB** |
| 5 — mono-safe | mono sum survives | worst **−3.1 dB** at default Spread; **−5.0 dB** at Spread 180 + Width 160. **Nothing in this device cancels on a mono sum** — 0 of 48 voicings carry the mono-hostile tag, by measurement |
| 6 — loop gain / nothing free-runs | 60 s of max-feedback noise, then silence | bounded and finite on all 6 (peak **0.04–0.26**); **−280 dBFS** 18 s after the input stops, all 6 |
| 7 — CPU | µs/block @ 48 k/128 | Step 11.05 · Endless 11.73 · Jet 11.74 · Envelope 13.10 · Tape Zero 13.28 · **BBD 21.96** = **0.41 – 0.82 % of one core** |
| — sample rates | 44.1 / 96 kHz | through-zero null **−60.2 / −62.4 dB** |

---

## 5. Correspondence with the other two fx3 devices

- **Shared vocabulary, same meaning, same name:** `Rate` · `Depth` · `Feedback` · `Mix` · `Width` ·
  `Low Cut` · the 20-entry sync table (`Free`, `4 bar` … `1/256`) with identical labels.
- **`Spread`** here means the L/R offset of the modulator, which is what it means in `DelayEngine.h`
  and `FilterFxEngine.h`. ⚠️ The Phaser bible uses `Spread` for **allpass stage stagger** and
  `Stereo` for the L/R offset; the Chorus uses `Phase`. Three names for one concept and one name for
  two concepts — an integration-owner call, flagged in `FINDINGS.md`.
- **Territory:** this device owns short delay + high feedback + through-zero. It does not do the
  ensemble job (multi-voice choruses, static detune stacks, June/Dimension voicings) and it has no
  allpass chain anywhere.
- ⚠️ **`Step` vs the Phaser's `Steps`** — the same concept in two devices, one letter apart. Flagged.
