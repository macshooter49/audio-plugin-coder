# WIDEN — the locked roster (chain kind 10, param prefix `SYN_WID_`)

**fb422 — THE FIX ROUND.** Six Types × eight Characters × six Fields, plus 3 front heroes + Mix
and 8 back knobs. Engine: `TerrainWidenFx.h`. Certification: `widen_cert.cpp` →
`widen_cert_fb422.log` (**206 PASS / 5 FAIL** — the five are named in `FINDINGS.md` §0 and are real).

🔴 **fb421 was REFUTED and this file carries the corrections.** All 15 WIDEN rows of
`Design/fx4/RENAMES.md` are applied verbatim. **Every knob label, Type name, Character name and
Field name in this document is now published FROM `TerrainWidenFx.h`** — `typeNames()`,
`charNames()`, `fieldNames()`, `frontNames()`, `backNames()`, `pillNames()`, `voicesUnit()`. If
this file and the header ever disagree, **the header wins and this file is the bug** (FIXES §3).

Every number in this file was measured by `widen_cert.cpp`, and every law-1 / law-4 / R11 gate in
it is proven to FAIL against a deliberately broken engine — see **`MUTATION.md`**.

---

## 0. What the device is, in one paragraph

**N copies of the input differing in pitch, in time, and in stereo placement.** The two lineages
Max named are Types 0 and 1: `Stack` is the Roland JP-8000 Super Saw played as an *audio* effect
(detuned voices oscillating sharp/flat, the measured uneven fan, the measured centre/sides mix
law), and `Twin` is the Roland SDD-320 Dimension D (a BBD chorus wired so it does **not** sound
like one — **triangle** LFO in antiphase across the channels, the delayed signal cross-mixed to the
opposite channel with **opposite polarity**). The other four are the mechanisms neither Serum nor
the SDD-320 has.

**It is not the Chorus.** Chorus (chain kind 6, shipped fb411) is **one audible cyclic voice
pair**. Failing to be that needs either (a) more than one pair of copies, or (b) copies that do
not *cycle*.

🔴 **fb422 CORRECTION.** fb421 claimed here that "the boundary is enforced in the DSP, not in
prose… Cert §O asserts it." **It did not.** §O measured `nV_` — a published integer — on `Stack`
and `Twofold` only, i.e. not on `Twin`, whose line count is a completely different expression, and
not on `Steady`. What is true now:

- `Voices` floors at **3 copies** on the three voice-crowd Types (`Stack`, `Steady`, `Twofold`) —
  the un-modulated centre read plus *two* movers.
- On `Twin` the count is **lines**, and the Characters that name a count now deliver it exactly at
  the `Voices` floor: **`Two Line` = 2 · `Four Line` = 4 · `Hex` = 6**. `Two Line` is genuinely a
  single pair — the literal SDD-320 — and it clears the boundary on clause (b), not (a): its
  triangle parks the detune at ±c so **1.2 %** of the spectrum is left on the carrier, against a
  sine-modulated control (`Wobble`, matched cents) at **12.5 %** and the arcsine law's closed-form
  prediction of 19 %.
- `Blur` and `Bands` shift nothing at all (carrier mass **1.000**) and cannot be a chorus by
  construction.
- The engine publishes `liveCopies()` — the REAL count of the machine that is running
  (`2·nPair_` on Twin, `nAP_` on Blur, `nB_` on Bands) — and §O cross-checks it against the
  OUTPUT. `WIDEN_MUT_NOFLOOR` proves the whole section goes red when the floor is removed.

---

## 1. THE TYPES (6 live · `SYN_WID_TYPE` ships as **choice(8)**, slots 6–7 reserved and disabled)

`kNumTypes = 6`, `kNumTypeSlots = 8`, `kNumChars = 8` for every Type. Cardinality is frozen at
birth (fb342): the param is created at 8 on day one so a 7th/8th Type can be added later without
breaking a single saved preset's normalised index. Character is read back on the **/7 scale** and
clamped per Type — the `fxrRestoreDistortion` idiom, never the dropdown's option count (fb373).

| # | Type | Lineage | The MECHANISM (what is physically different) | THE measurable discriminator | Measured |
|---|------|---------|---------------------------------------------|------------------------------|----------|
| 0 | **Stack** | Roland JP-8000 Super Saw, 1996 (Szabo's measured fan + mix law) | 3–8 micro-delay voices on one ring; each voice has its own **sine LFO at a scattered rate**; depth solved from the constant-cents law | **periodic** pitch motion, large modulation energy | modEng **1335 cents**, periodicity **0.869** |
| 1 | **Twin** | Roland SDD-320 Dimension D, 1979 · BOSS DC-2 · Xfer Dimension Expander | 1–4 line **pairs**, each pair modulated by a **TRIANGLE in antiphase** (`dL = d0+m`, `dR = d0−m`), then the delayed signal **cross-mixed to the other channel with inverted polarity** | **motionless**: flux at or below the crowd Types, yet side/mid **positive**; pitch histogram **bimodal** | flux **43.70** (Stack 44.06), side/mid **+11.7 dB**, lobe mass **0.992** |
| 2 | **Steady** | Eventide H3000 / AMS DMX 15-80s / Soundtoys MicroShift | 3–8 **granular constant-ratio** readers at **static** cents offsets, ±c fanned across the field, on unequal slap delays | **static**: essentially zero modulation energy, sidebands do not move | modEng **0.000 cents** |
| 3 | **Twofold** | ADT / Waves Doubler (its user guide's own ranges) | 3–8 discrete voices on **long static delays 17–61 ms**, pitch moved by a **band-limited random WALK** | **aperiodic** motion — no line in the modulation spectrum | periodicity **0.176** vs Stack **0.869** |
| 4 | **Blur** | Polyverse Wider · Ozone Stereoize · DAFx-24 StereoWidener | **two first-order allpass cascades** (up to 24 stages) whose break frequencies diverge with Amount. Phase only — no delay line at all | per-channel magnitude is **FLAT by construction** (allpass: \|H\| = 1 for any \|c\| < 1) | chan ripple **2.6 dB** (bypass control **0.0 dB**) |
| 5 | **Bands** | Orban 245E Stereo Synthesizer, 1975 · Schroeder 1958 | a **one-pole crossover TREE** splits the mid into 3–16 bands; alternate bands are pushed to opposite channels. `lp + (x−lp) = x` exactly, so the mono sum is the input **bit for bit** | per-channel magnitude is **TORN** — the anti-Blur | chan ripple **8.1 dB**, 3.1× Blur; mono deviation **0.000 dB** |
| 6 | *— reserved —* | | disabled in the UI; exists only so cardinality never has to change | | |
| 7 | *— reserved —* | | " | | |

**Cross-type distinctness** (cert §C, perceptual L2 over 8 phase-independent features, 1.0 = one
audible step): every pair ≥ **2.43**. Closest pair `Blur`/`Bands` at 2.43 — and they still differ
**3.1×** on the stated discriminator. The full 6×6 matrix is printed in the log.

### Types considered and CUT

- **`Ensemble`** (Solina/ARP triple chorus, 3-phase LFO pair) — **cut for two reasons, both hard.**
  (1) `Ensemble` is already a Type name on the shipped Chorus device: an absolute no-doubles
  violation across the rack. (2) Its mechanism is a *configuration* of Stack (locked-phase LFOs at
  two rates), not a different machine — exactly the blur CONTRACT §3.2 warns about. It survives
  where it belongs: **Stack / `Three Phase`**, which measures the largest Character distance in the
  entire roster (673.06).
- **`Rotor`** (Leslie-style Doppler crossfade panner) — cut by the bible's own audit; it belongs to
  a rotary/phaser device. The slot is *reserved*, not filled with padding.
- **`PV Glass`** (phase-vocoder micro-shift Character on `Steady`) — **cut, and it was never a
  close call.** `ShimmerPV` carries `PLAT = 1536 samples` = **32 ms of latency**, and the rack
  forbids reporting latency (the fb305 main-send exclusion subtracts the routed dry
  sample-aligned). 32 ms turns a ±10-cent double into a slapback and cannot be compensated. Its
  ~1–2 % CPU would also have been 3–5× the entire rest of the device.

---

## 2. THE CHARACTERS (8 per Type — each re-wires PHYSICS, never "a tone")

Cert §G measures every one of the 48 against its Type's default on the same 8-feature fingerprint.
**0 weak cells**; weakest is `Bands`/`Rotor Fast` at 0.60 (threshold 0.35). Distances in brackets.

### Stack — the crowd
| Character | What it re-wires | [dist] |
|---|---|---|
| `JP Classic` | the measured JP-8000 fan `−202/−112/−34/0/+34/+104/+177` cents, normalised to the live voice count | default |
| `Even Fan` | linear-spaced offsets instead — audibly phasier, and it is the A/B that proves the JP unevenness matters | 27.03 |
| `Analog Drift` | ±15 % random skew on every voice's LFO rate | 105.87 |
| `Tight Fan` | base scatter ×0.45 and clock ×1.35 — glassy, metallic | 74.68 |
| `Wide Fan` | base scatter ×2.2, clock ×0.70 — thick, doubler-adjacent | 30.56 |
| `Octave Bloom` | the top two LIVE voices read at **+1200 cents**, −12 dB | 10.02 |
| `Sub Anchor` | voice 1 at **−1200 cents**, forced to centre, −9 dB (the hardstyle trick) | 4.50 |
| `Three Phase` | the Solina law: all voices on ONE clock at 0/120/240°, plus a fast shallow second LFO — two lines in the modulation spectrum instead of a scatter | 673.06 |

### Twin — the SDD-320
| Character | What it re-wires | [dist] |
|---|---|---|
| `Two Line` | 2 lines. The literal SDD-320 | default |
| `Four Line` | +1 pair (the Duda four-liner) | 1.86 |
| `Mode Two` | Arturia, verbatim: *"the delay times are about half those of Mode 1"*. Under the constant-cents law a shorter delay at the same detune **is** a faster clock, so this is rate ×2.2 | 7.05 |
| `Mode Three` | Arturia, verbatim: *"a modulation intensity by the LFO that is twice that of modes 1 and 2"* — cents ×2 | 4.97 |
| `No Compander` | the 2:1 compressor and the tilt both bypassed — clean, modern, and the reference for what the compander is doing | 10.88 |
| `Dark BBD` | reconstruction LP at 4 kHz on the wet | 1.37 |
| `Wobble` | **triangle → sine.** The deliberate A/B that shows why the original is a triangle: the detune stops holding and starts breathing | 13.20 |
| `Hex` | 3 extra pairs and cross-mix ×1.55 — the wet mid inverts. **TAGGED mono-hostile** | 4.47 |

### Steady — static micro-pitch
| Character | What it re-wires | [dist] |
|---|---|---|
| `Satin` | 30 ms granular window (the H3000 lineage) | default |
| `Jab` | 12 ms window — tighter, glitchier transients | 2.52 |
| `Warble` | +0.55 of intrinsic Roam on the slaps | 4.18 |
| `Fifth Up` | the top two live voices at **+700 cents**, −15 dB | 6.26 |
| `Octave Down` | every shift negative, magnitudes unequal L/R | 4.03 |
| `Wide Slap` | slap delays ×2.3 | 2.65 |
| `Gritty` | linear interpolation + an 8 ms window — deliberate AM grit | 2.47 |
| `Octave Pair` | the top two live voices at **±1200 cents**, −12 dB | 8.55 |

### Twofold — the aperiodic crowd
| Character | What it re-wires | [dist] |
|---|---|---|
| `Vocal` | 17/29/41/53/23/35/47/61 ms, walk at 0.4–2.8 Hz | default |
| `Wide Room` | delays ×1.35, walk bandwidth ×0.5, clock ×0.7 | 118.52 |
| `Tape ADT` | walk depth ×2 with a 0.6 Hz wow component | 242.28 |
| `Tight Inst` | delays ×0.5 | 0.85 |
| `Loose Crowd` | delays ×1.8, walk ×1.6 | 179.95 |
| `Static Pair` | the walk is **replaced** by a fixed ±8-cent split — deliberately crosses into Steady territory, but with echoes | 297.42 |
| `Slapback` | voice 1 pushed to 48 ms — the Sun Records edge | 4.51 |
| `Seasick` | walk ×2.5 → **±155 cents** at Amount 100 %. Past useful, on purpose | 686.95 |

### Blur — phase decorrelation
| Character | What it re-wires | [dist] |
|---|---|---|
| `Smooth Six` | 3 stages per voice (9–24 total), 180 Hz–5.6 kHz | default |
| `Deep Twelve` | 6 stages per voice | 2.94 |
| `Velvet` | 4/voice, 90 Hz–9 kHz, +0.4 intrinsic Roam — the DAFx-17 sparse flavour | 2.97 |
| `Low Anchor` | cascade starts at 500 Hz — bass stays dead centre | 3.08 |
| `Top Only` | 2–10 kHz only | 3.30 |
| `Seed B` | 240 Hz–7 kHz — a different room | 1.16 |
| `Seed C` | 140 Hz–4.2 kHz | 2.12 |
| `Opposed` | path B's allpass coefficients negated. **Tag removed by measurement** — see FINDINGS | 4.50 |

### Bands — spectral alternation
| Character | What it re-wires | [dist] |
|---|---|---|
| `Coarse` | one band per voice (3–8 bands), grid 140 Hz–11 kHz | default |
| `Fine` | two bands per voice (6–16) | 1.68 |
| `Tilted` | ±18 % gain tilt between the odd and even band sets | 1.42 |
| `Rotor Slow` | grid sweep ×0.3 | 1.22 |
| `Rotor Fast` | grid sweep ×4 | 0.60 |
| `Guard` | contrast capped at g = 0.75 — the polite counterexample, and the only knob position in the device that is deliberately safe | 2.80 |
| `Deep Grid` | grid shifted down to 50 Hz | 1.45 |
| `Hard Split` | contrast law `sqrt(t)` — reaches full tear in the first half of Amount | 1.77 |

---

## 3. FRONT: 3 heroes + Mix + 2 pills

| Slot | Name | Relabel per Type | Law | Measured 0→100 |
|---|---|---|---|---|
| Hero 1 | **`Amount`** | Stack `Detune` · Twin `Depth` · Steady **`Cents`** · Twofold `Sway` · Blur `Wash` · Bands `Split` — **published by `frontNames(type)`** | per-Type, §4 | 🔴 **fb422 RE-VOICED (R11).** Measured on the OUTPUT SPECTRUM at Amount 100 %, Width 0.5 (unity): Stack **106.5 cents** · Twin **163.9** · Steady **144.3** · Twofold **55.8** · Blur corr **−0.87** · Bands corr **−0.55**. Control through the same engine at Amount 0: **1.7 cents / +1.000**. fb421 gave `Twin` **28 cents** — a mid-depth chorus on the Type Max named |
| Hero 2 | **`Width`** | — | equal-power M/S rotation, `θ = Width · π/2`. **0 = mono · 0.5 = EXACTLY neutral · 1.0 = SIDE ONLY** | side fraction **0.000 → 1.000** |
| Hero 3 | **`Rate`** | — | 🔴 **fb422: 0.08–14 Hz log** (0.03 Hz is a 33-second cycle — a dead zone no ear and no probe can resolve). And it now has a JOB ON EVERY TYPE: Stack/Twin the LFO · **Steady the GRAIN CLOCK** (the crossfade span 90 → 6 ms, ~0.7 → 11 Hz) · Twofold the walk bandwidth (0.12 → 6 Hz) · **Blur the allpass-field breath** (±0.5 octaves, 0.2 → 35 Hz) · Bands the crossover-grid sweep, **per sample** | at fb421 it was **BIT-IDENTICALLY DEAD** on `Steady` and on `Blur` |
| 4th | **`Mix`** | — | equal power; dry gain is `cos(π/2)` = **exactly 0** at 1.0 | dry rejection **0.0 → 142.4 dB** |
| Pill 1 | **`Retrig`** | — | note-on resets every voice phase; the READ POSITION is slew-capped at 0.5 samples/sample so the zap is identical at every Detune | click **−41.2 dB of programme peak** |
| Pill 2 | **`Hear Mono`** | — | UI-side audition of `(L+R)/2` on both outs, 15 ms fade both ways. Costs the engine nothing |  |

> 🏷️ **`Steady`-the-Type's hero knob is `Cents`, not `Steady`.** A Type and its own hero knob sharing
> a word inside one device is the absolute no-doubles violation. `Cents` is also what the knob
> literally reads out.

---

## 4. BACK: 8 knobs (4×2) + 2 dropdowns

### The 8 knobs — and why each earns its slot

| P | Name | Range · curve | What it does | Why it beats the alternative | Measured 0→100 |
|---|---|---|---|---|---|
| **P1** | **`Voices`** | 3–8 copies, stepped | how many copies exist. **Twin → LINES** (`2·nPair_`, and the Character names are now the count: Two Line 2 / Four Line 4 / Hex 6) · Blur → allpass stages · Bands → bands (`1.6·(nV−1)`, so all five knob positions differ — fb421 gave 4/4/6/8/8, two dead steps). The engine publishes `liveCopies()` and `voicesUnit(type)` | The single most defining control of a *crowd*. **Floors at 3** because 2 copies is a chorus (CONTRACT §4) | every step moves the mono spectrum **10.0 / 14.3 / 13.5 / 14.9 dB** |
| **P2** | **`Spread`** | 0–1, `t^0.65` | fans the copies across the field. A **ladder**: each successive voice sits further out, and at fb422 it goes **PAST hard pan** (reach 1.60×, clamped at ±1.40) into ANTIPHASE placement. On `Twin` it is the pair's placement, on `Blur` the separation of the two decorrelated paths, on `Bands` the **spectral extent** of the split (opening from the middle outward) | Distinct from `Width`, which is a matrix on the finished wet. Spread is *geometry*; Width is *rotation* | 🔴 1−corr **0.000 → 0.947** (Stack) · **0.000 → 1.891** (Twin) · **0.000 → 1.823** (Blur) · **0.000 → 1.120** (Bands). fb421: **0.000 → 0.125**, and it was **dead on Twin, Blur and Bands** — `sprSm0_()` appeared only inside a `viz_.voicePan` assignment |
| **P3** | **`Offset`** | 0.25×–**4.0×**, log, unity at the centre | how far behind the copies sit — the delay length itself | Named `Offset`, not `Delay`: a back knob may not carry another rack device's name | wet time-centroid **2.79 → 35.09 ms** |
| **P4** | **`Roam`** | 0–1, `t^0.7` | a slow random walk on every copy's **time (±16 ms) and placement (±0.85 pan)**. fb422 gives it a home on the three Types that had none: `Twin` walks each LINE independently (breaking the antiphase symmetry the cross-mix lives on), `Blur` walks the allpass field, `Bands` walks the crossover grid | Time jitter alone measured on the trace and barely on the output; a crowd that does not move in *space* is not a crowd | 🔴 gated as OUTPUT CHANGE per quarter against one quarter-turn of `Mix`. fb421: **dead on Twin and Bands**, and on `Blur` it was a static re-dice, not a walk — its measured motion ran **backwards** (0.061 → 0.043) |
| **P5** | **`Low Keep`** | 0 (off) – **800 Hz**, log (fb422: R11) | everything below stays **mono and centred**, and is immune to Width and to Field | This is the device's own mono guard, and it only means that if the low band never enters the widening matrix at all — which is how it is wired | LF side energy **19.6 → 10.7 dB** |
| **P6** | **`Tone`** | ±12 dB tilt, 0.5 neutral | darkens/brightens **the wet only** (the MicroShift `Focus` job) | A real shelving tilt, not a lowpass crossfade — the first version's centroid stopped moving over the top half of the knob | HF−LF tilt **−33.0 → −55.3 dB** |
| **P7** | **`Feedback`** | 0–**0.985**, taper `1−(1−gmax)^t` — **linear in ln(TAIL)** | regenerates the copies into a bloom. Env-gated, tanh-bounded, 7 kHz damped, 10 Hz AC-coupled | 🔴 **fb422 found three separate reasons it was not a wall.** (1) The `Twin`/`Blur`/`Bands` **Balance anchor sat INSIDE the loop** as a zero-sample path — a one-sample resonator, not a bloom. (2) `Twin`'s **2:1 compander was inside the loop**, i.e. a limiter on the regeneration by definition: +0.31 dB across the whole knob. (3) `Blur` and `Bands` **never touch the ring**, so their loop was a DC shelf; they now regenerate through their own 23 ms line at two taps. The 7 kHz damping also cost 4.3 dB of broadband energy per pass and is now 14 kHz | 🔴 sustained density at `Voices` 100 %: Stack **+16.71** · Blur **+18.95** · Bands **+23.43** · Twin **+3.21** · Steady **+2.89** · Twofold **+2.03 dB**. The last two are **BELOW the +3 dB bar and are reported as such** — see FINDINGS §0 |
| **P8** | **`Balance`** | 0–1, the measured JP-8000 mix law | the un-modulated centre read vs the copies. At 100 % there is **no centre at all**. On `Twin` the anchor is the **un-delayed line** (fb421 cast `lineL`/`lineR` to `(void)` and the knob was dead there); on `Blur`/`Bands` it is the un-processed mid | Szabo's measured curve verbatim for the shape, faded to zero across the knob so the top quarter is not dead; sides normalised by `1/√(N−1)` so `Voices` is never a volume knob | side/mid **−37.3 → −9.4 dB** |

### Dropdown 1 — `Character` (8 per Type, §2)

### Dropdown 2 — **`Field`**, and it is NOT `Type`

`Type` is the header pill (`DEVS[].tp`). fb418 removed a back-panel `Type` duplicate from all three
fx3 devices for exactly this reason and it is not coming back (CONTRACT R6).

`Field` is the **placement matrix**: what stereo transform the finished wet is poured through. Every
option is a different matrix — physics, never tone — and it applies identically to all six Types,
so it is never a dead dropdown on any of them.

| # | Field | The matrix | Mono behaviour (measured, Stack, Mix 1.0) |
|---|---|---|---|
| 0 | `Direct` | identity | level −0.72 dB · worst notch −2.04 dB |
| 1 | `Alternate` | channels **swapped above a 700 Hz crossover** — frequency-dependent placement. A swap does not change `L+R`, so this option is mono-**exact** | −0.72 dB · −2.04 dB |
| 2 | `Orbit` | a true rotation of the L/R pair advancing at `Rate`. It sweeps **through** antiphase once per orbit — **TAGGED mono-hostile** for that reason | −5.40 dB · −0.92 dB |
| 3 | `Swap` | L ⇄ R | −0.72 dB · −2.04 dB |
| 4 | `Side Only` | mid forced to 0: `L = +s`, `R = −s`. **TAGGED mono-hostile** — a fold-down deletes the wet by definition, and that same property is what makes it the harness's exact dry-residual probe | **−142.42 dB** |
| 5 | `Collapse` | side forced to 0: `L = R = m`. A pure thickener — the *most* mono-audible setting in the device | −0.72 dB · −2.04 dB |

---

## 5. THE MONO MANIFEST — declared, never hidden

CONTRACT law 5 says a mono-hostile Type or Character must be **tagged and gated**. The engine
declares three predicates and the harness checks every entry against measurement:

```cpp
static bool typeIsMonoLossy   (int type);            // Twin · Twofold · Blur
static bool fieldIsMonoHostile(int field);           // Orbit · Side Only
static bool charIsMonoHostile (int type, int chr);   // Twin / Hex
```

| | measured at Mix 1.0, Amount 0.7 | why it is inherent |
|---|---|---|
| `Twin` | level **−8.84 dB**, worst notch **−14.86 dB** | the inverted cross-mix SUBTRACTS the channels. That subtraction *is* the width |
| `Twofold` | notch **−6.36 dB**, mean dev **6.80 dB** | discrete 17–61 ms copies comb; the walk moves the notches but not fast enough to average out |
| `Blur` | notch **−16.60 dB**, mean dev **3.20 dB** | two allpass cascades sum to a comb. DAFx-24 measures "1 to 2 dB" for mild settings; R11 asks for up to 24 stages |
| `Stack` | level −0.72 dB, notch **−2.04 dB** | **mono-safe**: the combs MOVE and average out |
| `Steady` | level −1.07 dB, notch **−1.17 dB** | **mono-safe**: static copies, but tiny ones |
| `Bands` | mean spectral deviation **0.000 dB at every Amount** | **mono-EXACT by construction**: `lp + (x−lp) = x`, and the band gains sum to 2 for any contrast |

---

## 6. Param IDs and cardinality (for the integration owner)

```
SYN_WID_TYPE        choice(8)   6 live + 2 reserved-disabled   default 0 (Stack)
SYN_WID_CHARACTER   choice(8)   always 8; read back on /7 and clamp to the Type's list
SYN_WID_FIELD       choice(6)   Direct · Alternate · Orbit · Swap · Side Only · Collapse
SYN_WID_AMOUNT      float 0..1  default 0.35
SYN_WID_WIDTH       float 0..1  default 0.50   ← 0.5 is EXACTLY neutral, not a compromise
SYN_WID_RATE        float 0..1  default 0.35
SYN_WID_MIX         float 0..1  default 0.50
SYN_WID_P1..P8      float 0..1  defaults 0.50 / 0.85 / 0.50 / 0.00 / 0.00 / 0.50 / 0.00 / 0.50
SYN_WID_POWER       bool        default OFF   (the distortion precedent)
SYN_WID_RETRIG      bool        default OFF
SYN_WID_MONO        bool        default OFF   (the `Hear Mono` audition pill)
SYN_WID_SRC_A..SRC_NOISE  bool  default OFF   (inherited per-osc routing)
```

`AudioParameterChoice` reads are **INDEX direct** — `(int) *rawParam(id)`, the CLAUDE.md §4 law and
the fb50 bug that proves it. Choices go over the wire normalised: write `idx/(N−1)`, read back
`Math.round(v·(N−1))`, and Character always on the **/7** scale.

There is **no Send param** — Send is positional and inherited (CONTRACT R8). There is **no Rate
sync** for v1: the house division table spans 0.125–128 Hz at 120 BPM and above ~15 Hz an LFO on a
delay line stops being a widener and becomes audible FM. The plumbing is in the engine, honoured
and clamped into 0.03–14 Hz, and simply not exposed.

## 7. Viz (CONTRACT §2, exactly these fields)

```cpp
struct Viz {
    float corr;                 // -1..+1 running stereo correlation of the OUTPUT
    float voicePan[8];          // -1..+1 live placement of each copy
    float voiceCents[8];        // live pitch offset, from the TRUE read-position slope
    float widthNow;             // 0..1 measured side/(mid+side) of the wet
    float lvl;                  // 0..1 wet level
};
```

`voiceCents` is computed from `1200·log2(1 − d'(t))` — the actual derivative of the read position —
and **not** from the modulator's shape. That distinction is load-bearing: the first version
published `achievedCents × sign(triangleSlope)`, which reports a perfectly bimodal detune whatever
the LFO is, and would have made the Dimension triangle test unfailable. The log2 runs on a 64-sample
telemetry tick, not per sample, because it is a card cost and not an audio one.
