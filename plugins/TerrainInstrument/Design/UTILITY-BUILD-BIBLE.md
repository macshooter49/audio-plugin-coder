# Terrain Instrument — Utility Build Bible

_The 4th FX device. The glue/gain-staging tool — the smallest device in the rack and the one that makes
the MULTI-DEVICE CHAIN epic work, because it is how a chain gets "routing properly leveled" (Max's words).
Research base: Serum 2 User Guide (local PDF, p.182), Ableton Utility (manual + the Cycling '74
`abl.device.utility~` port), Kilohearts Stereo/Haas/Gain snapins, Voxengo MSED, Airwindows PurestGain,
iZotope Ozone Imager/Insight, Waves PAZ, beis.de correlation-meter math, KA Electronics elliptical EQ.
Repo recon: every recycle claim below is verified by reading the code — file:line cited._

---

## 0. The scope decision

**One device, NO fake Types.** Utility is not a family of algorithms — it is a fixed signal chain of
small exact operations: channel routing, polarity, gain, cuts, tilt, M/S imaging, bass-mono, micro-delay
widening, balance. The two-dropdown chassis is solved **honestly** (per the mandate: law 5 cuts both
ways): the dropdowns are **Route** (channel matrix) and **Flip** (polarity matrix) — both discrete,
both sound-changing, every choice night-and-day measurable. There is no `Character` because there is no
character: a utility that "colors" is a broken utility. The color knob it does get (**Tilt**) is an
explicit, bounded, visible voicing move — not a hidden character.

**Why this device matters more than its size.** The FX bus program sits at **≈ −26 dBFS** (measured,
`PluginProcessor.cpp:46` + the fb299 comment; pad at `:6300-6301`). Three flagship devices now live on
that bus, serially permutable (`SYN_FX_ORDER`, `PluginProcessor.cpp:3488`, switch at `:7383`). Nothing
in the chain can currently *re-level between devices*: you cannot drive the distortion harder without
changing the patch, cannot trim a hot reverb before the delay, cannot mono a widened low end before it
hits the master limiter (`fb264`). Utility is that missing joint. It is also the **metering surface**
the rack has never had — the goniometer/correlation card is the first place a Terrain user *sees* the
stereo field.

**What Utility is NOT:** not a compressor (that device is next), not an EQ (the Equalizer bible owns
curves; Utility's Low/High Cut + Tilt are the Serum-2-sanctioned "quick clean" trio only), not a
modulation effect (nothing in it free-runs; there is no LFO).

---

## 1. History and circuits — the lineage

* **The console monitor/utility section.** Every large-format console ships a strip of "boring"
  blocks — phase (Ø) buttons, channel swap, mono sum, trim — because engineers check mono and polarity
  *constantly*. The DAW Utility device is that strip, extracted.
* **Blumlein, 1931.** Alan Blumlein's stereo patent (EMI, GB 394325) contains the M/S sum/difference
  matrix — `M=(L+R)/2, S=(L−R)/2` — which is the mathematical core of every Width knob ever shipped.
  Width is 95 years old and still just "scale S."
* **Elliptical EQ (vinyl cutting, 1950s-70s).** Stereo bass = vertical stylus modulation = skipping
  needles and wasted groove. Cutting chains inserted an **elliptic equalizer: a high-pass on the SIDE
  channel only** (KA Electronics documents historic 6 dB/oct units; modern practice 12 dB/oct,
  engagement points ~100-300 Hz). This is the correct ancestor of "Bass Mono" — not a band-split. Our
  `Mono Below` is a straight clean-room descendant.
* **Haas, 1949.** Helmut Haas's precedence-effect research: an identical signal delayed ≤ ~30 ms is not
  heard as an echo but as *width/position*. The Haas-widener (delay one channel a few ms) is the
  cheapest mono-to-stereo trick in existence — and the most mono-dangerous (comb filter on the sum).
  Kilohearts ships it as a dedicated snapin (Delay + which-channel selector).
* **Pan law (BBC/console era).** A dual-gang pot crossfading L/R. The classic variants: −3 dB
  (equal-power sin/cos, the common compromise), −4.5 dB (SSL), −6 dB (equal-voltage, exact mono sum).
  §3.2 states which we use for `Balance` and why unity-through wins over pan-law purity here.
* **Ableton Utility — the canon.** The reference feature set (Live 10+): Gain ±35 dB, Balance,
  **Width 0-400 %** (0 = mono), **Bass Mono with 50-500 Hz crossover** + headphone audition, Ø L / Ø R
  phase inverts, channel mode Left/Right/Swap/Stereo, DC filter, Mute. (Ranges cross-checked against the
  Cycling '74 `abl.device.utility~` port: bass-mono freq `[50., 500.]` Hz, channel enum
  `Left/Stereo/Right/Swap`; the port's gain range `[−70.6, +6]` documents the pre-Live-10 gain, the
  modern device is ±35 dB.) Producers describe Utility as the most-used device in Live. That reputation
  is the bar.
* **Kilohearts Gain / Stereo / Haas snapins.** The modular decomposition: Stereo = Mid + Width + Pan
  with a mini balance/correlation meter that goes red below zero correlation. Their "small tools done
  perfectly" ethos is the personality target.
* **Voxengo MSED.** The M/S surgery canon: encode/decode, inline Mid/Side gain + pan + mute/solo,
  plasma vectorscope. Proof that Mid gain and Side gain are *both* wanted (our `Center` + `Width`).
* **Airwindows PurestGain.** Chris Johnson's point: even a gain knob deserves engineering — his ships
  a smoothing algorithm "to completely eliminate zipper noise" and high-res internal processing. Our
  glide-everything law says the same thing.
* **iZotope Ozone Imager / Insight.** The modern visual language: three vectorscope modes
  (**Lissajous** per-sample dots, **Polar Sample** dots in polar coordinates, **Polar Level** rays where
  length = amplitude, angle = stereo position), 45° "safe lines" (inside = in-phase, outside =
  out-of-phase), correlation meter, plus Stereoize (Haas-class widener, mode I "colorful phasing" /
  mode II "subtle").
* **Serum 2 Utility (2025) — the direct competitor.** Manual p.182, exact list: **POLARITY INV**
  (per-channel L/R), **LPF**, **HPF**, **MONO BASS + FREQ** ("forces frequencies below the threshold to
  be monophonic"), **WIDTH**, **PAN**, **MIX**, **LEVEL** (dB). No dedicated visualizer beyond the
  module strip — **that is our opening**: Serum's Utility is knobs; ours is knobs + the only live
  goniometer in the product category's synth-FX racks.

---

## 2. Types — the honest two-dropdown answer

No invented families. The two selectors are the two *discrete* dimensions a utility genuinely has.

### 2.1 Dropdown 1 — `Route` (channel matrix, 6 states)

Each Route is a 2×2 mixing matrix `[out_L; out_R] = A·[in_L; in_R]`. The matrix coefficients are the
*only* state — which makes switching trivially click-free (§3.9).

| Route | Matrix A | What it does | Measurable discriminator (night-and-day) |
|---|---|---|---|
| **Stereo** *(default)* | `[[1,0],[0,1]]` | Pass-through | Bit-identical at defaults (the null gate, §7.1) |
| **Mono** | `[[.5,.5],[.5,.5]]` | Full mono sum (= Mid to both) | Side RMS → −∞; correlation → +1.00 exactly |
| **Side** | `[[.5,−.5],[−.5,.5]]` | Solo the difference signal (S / −S) | **Mono sum → −∞** (total null); correlation → −1.00 |
| **Left** | `[[1,0],[1,0]]` | Left to both ears | R input contributes 0; L/R outputs byte-identical |
| **Right** | `[[0,1],[0,1]]` | Right to both ears | Mirror of Left |
| **Swap** | `[[0,1],[1,0]]` | Exchange channels | L/R energy exchange, exact; gonio image mirrors |

*(Ableton's `Mid` audition and `Mono` collapse are the same matrix — we ship it once, named Mono.)*

### 2.2 Dropdown 2 — `Flip` (polarity matrix, 4 states)

Applied after Route as a diagonal sign matrix.

| Flip | Signs | Use | Discriminator |
|---|---|---|---|
| **None** *(default)* | `+ +` | — | identity |
| **Left** | `− +` | Fix a polarity-flipped source; creative anti-phase | Correlation of correlated material: `r → −r`; mono sum combs/cancels |
| **Right** | `+ −` | Same, other channel | Mirror |
| **Both** | `− −` | Absolute polarity flip | Waveform inverts (audible on asymmetric material — kicks, brass, voice); correlation unchanged |

**Rejected alternative for dropdown 2:** a Scope/meter-view selector (Lissajous / Polar / Meters).
A dropdown that changes no sound is a dead control by the spirit of law 5 — the meter view belongs on
the card itself as small corner tabs (§6.4), where clicking a *view* is a view gesture, not a
parameter. Flagged for Max in §12 anyway.

---

## 3. DSP core — algorithms, math, param laws

### 3.0 Signal flow (fixed order, load-bearing — see §7.3 for why)

```
in L/R
 → [DC pill]            10 Hz DC blocker (DCBlocker, TerrainFilters.h:69; r-law DistortionEngine.h:124)
 → Route matrix          glided 2×2 (§2.1)
 → Flip matrix           glided signs (§2.2)
 → Gain                  linear gain, one-pole 15 ms glide (§3.1)
 → Low Cut / High Cut    12 dB/oct SVF pair, both channels (§3.6)
 → Tilt @ Pivot          complementary dB-symmetric shelves (§3.5)
 → M/S block:            encode M=(L+R)/2, S=(L−R)/2
      Center             mid gain (§3.4)
      Width              side gain ×0..4 (§3.3)
      Rotate             energy-preserving field rotation (§3.8)
   decode
 → Widen                 Haas micro-delay ±30 ms (§3.7)
 → Balance               opposite-channel attenuation (§3.2)
 → Mono Below            2nd-order side-HPF — LAST imaging op, enforces the contract (§3.4)
 → [Mono pill]           width→0 override, glided
 → Mix                   equal-power dry/processed (sin/cos idiom, PluginProcessor.cpp:7112)
out L/R
```

No feedback path exists anywhere in the device. **Unconditionally BIBO; max stable loop gain: N/A —
there is no loop** (law 6 satisfied by construction; nothing can sound after the input dies because
every block is memoryless or a decaying linear filter).

### 3.1 Gain — the hero knob, in dB, on the real bus

House law 1 applied: the bus program is −26 dBFS, so the knob must be able to *put program at and past
full scale*, i.e. reach ≥ +26 dB, and must be linear-in-dB (the fb315 lesson: `1+amt·k` multipliers are
why Terrain ever shipped timid).

```
t = 0            → −∞ (hard mute; glided like everything else)
t ∈ (0, 0.04]    → ramp −∞ → −60 dB   (the "reach silence" tail)
t ∈ (0.04, 1]    → gainDb = −60 + 96·(t−0.04)/0.96      // linear in dB, 1 dB per ~1 % of travel
unity 0 dB at t ≈ 0.665  → knob DETENT + double-click reset point
max: +36 dB
```

+36 dB on a −26 dBFS program = +10 dB over full scale into the next device or the fb264 limiter —
deliberately past useful (law 5's "just past useful"; the limiter bounds it, and *slamming the next
device is the point* — §7.2). Glide: one-pole ~15 ms on the **linear** gain (dB-domain glide is
slower near −∞ and can sound like a fade-curve change).

### 3.2 Balance — the pan law decision

`b ∈ [−1, +1]`, center detent. **Balance law (attenuate the opposite channel), not a re-pan:**

```
gL = (b <= 0) ? 1 : cos(b·π/2)
gR = (b >= 0) ? 1 : cos(−b·π/2)
```

* At center both channels are **exactly unity** — this is what makes the whole device null at defaults
  (§7.1). A −3 dB equal-power pan law puts a dip at center and breaks unity-through; compensating it
  (+3 dB at extremes) makes hard-pan land +3 dB hot into the next device. Rejected.
* The attenuation curve is the equal-power quarter-cosine, so the travel is perceptually even
  (no dead first third).
* Full throw = one channel at −∞: a stereo source becomes one-sided — dramatic, mono-sum drops ~6 dB
  of the removed channel's exclusive content. Correct behavior for a *balance*.
* This matches Ableton/Serum semantics (`PAN` = "stereo balance", Serum 2 manual p.182).

### 3.3 Width — M/S side gain, 0-400 %

Blumlein matrix, wet path only, exactly the shipped delay idiom (`DelayEngine.h:208-210`):

```
M = 0.5·(L+R);  S = 0.5·(L−R)·w;   L' = M+S;  R' = M−S
w:  t ∈ [0, 0.5] → w = 2t          (0 → 100 %, linear — mono→unity)
    t ∈ (0.5, 1] → w = 1 + 6(t−.5) (100 → 400 %, side boost up to +12.04 dB)
detent 100 % at t = 0.5 (default)
```

* **The mono-sum invariant (state it, test it, show it):** `(L'+R')/2 = M` for *any* w. Width — even
  400 % — **never changes the mono sum**. The mono danger of overwidening is not the sum, it is
  (a) correlation dropping toward/below 0 (speaker-placement dependent combing in the room) and
  (b) side-heavy mixes losing most of their energy on collapse. The correlation meter (§6) is the
  honest companion; this is why the meter lives on this card and not in a menu.
* We do **not** touch M as w rises (no hidden "compensation") — the knob does exactly one thing.
  400 % = +12 dB side: on normal program the image detaches from the speakers and the correlation bar
  visibly dives. That is the drama, live on the card.
* 0 % = true mono (equivalent to Route=Mono but continuous and glidable — the automation lane version).

### 3.4 Center (mid gain) + Mono Below (the elliptical side-HPF)

**Center** — the complement MSED proves people want:

```
t ∈ [0, 0.02]  → −∞ (mid kill: instant karaoke/side-only)
t ∈ (0.02,0.5] → centerDb = −72·(0.5−t)/0.96 ≈ −36 → 0 dB
t ∈ (0.5, 1]   → centerDb = +24·(t−0.5)   (0 → +12 dB)
default/detent 0 dB at t = 0.5
```

Min = the classic vocal-drop/karaoke gesture (dramatic, one knob); max +12 dB = mono-forward "glue".
Applied to M inside the M/S block. Note Center and Width together can reconstruct any M/S balance —
that redundancy is a feature (MSED ships both), not a dead knob: their *discriminators differ*
(Center moves the mono sum, Width cannot — measurably orthogonal).

**Mono Below** — the bass-mono knob, elliptical-EQ lineage:

```
t = 0        → OFF (no filter in the path at all — exact bypass)
t ∈ (0, 1]   → fc = 20 · 25^t  Hz   (20 → 500 Hz, exponential; Ableton's law-checked 50-500 range
                                     sits in the top ~60 % of travel; 20-50 covers rumble-only duty)
S_out = HP2(S, fc, Q = 0.7071)       // 2nd-order Butterworth high-pass ON THE SIDE ONLY, 12 dB/oct
```

* **Why side-HPF and not a band-split:** an LR4 crossover + mono-ized low band costs 4 biquads, adds
  phase rotation to the mid, and buys nothing audible over the elliptical approach; the side-HPF is
  ONE biquad on one (side) channel, leaves M bit-untouched, and IS the historical circuit
  (KA Electronics; mastering side-HPF practice ~100-300 Hz). Below fc the image collapses to center at
  12 dB/oct; the mono sum is unchanged by construction.
* Placed **last** in the imaging chain (§3.0) so it disciplines everything upstream — including the
  Haas widener and Rotate. "Bass is mono" is a contract; the only way to honor it is to enforce it
  after every widening op. (This ordering is the fix for club/vinyl translation — the reason the
  feature exists: single-sub club PAs and vinyl lathes both collapse/reject stereo lows; stereo bass
  arrives as phase cancellation or a skipping groove.)
* Engage-from-off: crossfade `S ↔ S_hp` over 15 ms (a filter snapping into a live path with zeroed
  state is a click; the crossfade is the standard cure). fc glides at coefficient level, ~20 ms.
* Measured discriminators: side energy below fc falls 12 dB/oct; correlation measured in the low band
  → +1.0; goniometer cloud's low-frequency excursion visibly narrows (the viz draws it, §6.4).

### 3.5 Tilt + Pivot — the voicing knob

Recycle the shipped grammar wholesale: the delay's output tilt (`DelayEngine.h:262-263`, one-pole split
~760 Hz) and the distortion's dB-symmetric Emphasis pair (`DistortionEngine.h:333`: "±18 dB of tilt
hinged ~1.2 kHz. **±6 dB would be the timid version**", fb325 dB-symmetric fix at `:630`).

```
lp = TPTOnePole(x, fPivot)                      // TerrainFilters.h:83
y  = gLo·lp + gHi·(x − lp)
gHi = 10^(+tiltDb/20),  gLo = 10^(−tiltDb/20)   // dB-symmetric — NEVER the linear (1+e·k) form,
                                                // which hits exactly zero at one end (fb325 measured)
tiltDb: bipolar ±18 dB, detent 0 at t = 0.5     // the house emphasis reach; ±6 is the timid version
Pivot:  fPivot = 150 · 10^t  Hz  → 150 Hz … 1.5 kHz, default ≈ 700 Hz (the distortion Tone hinge,
                                   DistortionEngine.h:988)
```

* One pole + complementary gains = a true tilt (6 dB/oct skirts, flat sum at 0) for ~4 flops/sample.
* `Tilt`/`Pivot` are a **matched pair** (the Mod Depth/Rate law): Pivot is conditionally inert at
  Tilt = 0, exactly as Mod Rate is at Depth 0 — sanctioned.
* At +18: brilliance/thinner; at −18: the "Seventies" felt-dark tilt. On the −26 dBFS bus these are
  pure spectral tilts with zero clipping risk inside the device (linear!), so no extra headroom logic.

### 3.6 Low Cut / High Cut — the Serum-parity quick-cleans

Serum 2's Utility ships LPF + HPF (manual p.182) — so must ours; these are the "strip the boom / strip
the fizz *right here between two devices*" moves that the big Equalizer device is too heavy for.

```
Low Cut:  t = 0 → OFF;  else fc = 20 · 50^t  Hz  (20 → 1000 Hz)   HP, 12 dB/oct
High Cut: t = 1 → OFF;  else fc = 200 · 100^(t) Hz (200 → 20 kHz)  LP, 12 dB/oct
```

Both via `SvfMultimode` (`TerrainFilters.h:317`), Q = 0.7071 (no resonance — a utility never peaks),
coefficients recomputed block-rate on a change-gate and slewed (the house reverb pattern). The Low Cut
range mirrors the reverb/delay `Low Cut` 20-1000 Hz grammar (`ParameterIDs.hpp:381`). Fully crossed
(Low Cut 1 kHz + High Cut 200 Hz) the passband is gone and program drops to a resonance-free
mid-band residual ~−25 dB and falling — deliberate destruction allowed; both knobs stay live the whole
way (no plateau: every degree moves a corner).

### 3.7 Widen — the Haas micro-delay, bipolar

One knob, channel choice folded into the sign (Kilohearts ships a separate channel selector; a bipolar
knob is the same information with a centerline):

```
v ∈ [−1, +1], detent 0
|v|·30 ms = delay;  v < 0 delays LEFT, v > 0 delays RIGHT;  |delay| < 0.25 ms = off (true bypass)
Fractional delay, cubic interpolation (the Delay HQ idiom), 2048-sample buffer @48 k.
Delay-length changes: DUAL-TAP 15 ms equal-power crossfade (the comb-click law — never slide the
read head; a sliding head is a pitch chirp).
```

* Perceived: 1-10 ms = width/thickening; 10-30 ms = doubling into slapback's doorstep. 30 ms is
  deliberately past the "safe" 15 ms textbook ceiling (law 5).
* **The mono trap, stated with numbers:** mono sum of `x(t) + x(t−τ)` combs with notches at
  `f = (2k+1)/(2τ)` — at τ = 10 ms the first null is 50 Hz and nulls repeat every 100 Hz. This is the
  ONE control in the device that damages the mono sum. Mitigations shipped, not hidden: (a) `Mono
  Below` sits after it and re-mono-izes the lows (κ the first nulls land in bass — protect it),
  (b) the correlation bar and the Mono pill make the damage a one-glance/one-click check. We do NOT
  auto-compensate (no hidden allpasses) — honesty + a meter beats magic.

### 3.8 Rotate — energy-preserving field rotation

The Kilohearts-Stereo-lineage move Balance cannot do:

```
θ ∈ [−45°, +45°], detent 0
M' = M·cosθ − S·sinθ
S' = M·sinθ + S·cosθ
```

* Rotates the *goniometer image itself* (the viz literally rotates with the knob — the most
  param-reflecting control on the card). At ±45° a centered mono source lands hard on one diagonal =
  fully panned, but **wide material rotates as a field** — sides swing through center to the other
  side, which balance/pan cannot produce.
* Energy-preserving (rotation matrix, det = 1): no level ambiguity, nothing to compensate.
* Discriminator vs Balance: Balance attenuates a channel (total energy drops as you throw); Rotate
  preserves total M/S energy and *relocates* it. Measured: hard-throw Balance loses the opposite
  channel's exclusive energy; hard Rotate keeps total RMS within 0.1 dB while the inter-channel
  energy ratio swings.
* On already-hard-panned program, past-diagonal rotation folds the image (a Side-heavy signal rotates
  into anti-phase territory) — allowed, visible on the correlation bar.

### 3.9 Switch/glide law — everything glides, nothing clicks

* **Route/Flip = glided matrices.** The combined Route·Flip 2×2 coefficients are targets; each of the
  4 coefficients runs a one-pole ~10 ms glide. A switch is then a smooth morph through valid
  intermediate mixes — click-free *by construction*, no crossfade machinery, no state to re-seat
  (there is none). This is cheaper and safer than the distortion's deferred-fade dance because the
  device is memoryless at the matrix.
* Gains (Gain, Center, Width, Balance, Mix, Tilt shelf gains): one-pole 15 ms on linear values.
* Filter cutoffs (cuts, Pivot, Mono Below): block-rate coefficient recompute on a change-gate,
  one-pole slew — the reverb-core pattern.
* Widen: dual-tap crossfade (§3.7). Pills (Mono, DC): 30 ms equal-power fades.
* Mix: equal-power `sin/cos(mix·π/2)` — the exact shipped idiom (`PluginProcessor.cpp:7112`,
  `:7274`), 100 % = fully wet (law 4).

### 3.10 Oversampling verdict

**None. Ever.** Every block is linear and time-invariant (matrices, gains, biquads, a fractional
delay) — the device generates **zero** harmonics and therefore zero aliasing. Oversampling would be
pure CPU waste (the reverb bible's "never oversample a linear network" law applies to the whole
device). There is no Quality dropdown. This is also the CPU story (§9): Utility is the cheapest
device in the rack by an order of magnitude.

### 3.11 Denormals / DC / NaN

* States that recirculate: 6-8 biquad/one-pole states + the Haas buffer. `ScopedNoDenormals` in the
  block + flush-to-zero on the filter states (house standard). No feedback ⇒ no denormal *bloom*.
* The device does not create DC (all ops linear, no asymmetry); the **DC pill** exists for *incoming*
  DC (FM/fold/asymmetric distortion upstream — exactly why `DistortionEngine` carries its own
  blockers at `:965`/`:2706`). 10 Hz one-pole DCBlocker, r-law clamped per `DistortionEngine.h:124-125`.
* No NaN paths (no divisions by signal, no transcendentals of signal). `cos/sin/pow` only at
  block-rate parameter maps.

---

## 4. ⚡ NO PLAYING SAFE — the extremity table

The bus is −26 dBFS (law 1). Every ceiling below is set where the move stops being useful to
*somebody*, not where it stops being polite.

| Param | What **100 %** (or full throw) actually does | The dangerous zone | Failure point / clamp (stability only) |
|---|---|---|---|
| **Gain +36 dB** | −26 dBFS program leaves at +10 dBFS: slams the next device or rides the fb264 limiter into loud-and-flat. As chain-first, it is a drive knob for the whole rack | +20…+36 into Distortion = free extra drive stage; into Reverb = tank overload colour | None needed — downstream limiter bounds; device itself is linear |
| **Gain t=0** | −∞: a glided MUTE (automatable stutter tool) | — | Glide keeps it click-free |
| **Width 400 %** | +12 dB side: image detaches from the speakers, correlation dives toward/below 0, room-dependent swirl | 250-400 % on synth pads = the "outside the head" trick | Mono sum PROVABLY untouched (§3.3) — no clamp; the meter is the guardrail |
| **Center −∞** | Mid killed: instant karaoke / side-wash. On a mono patch: **silence** (side of mono = 0) — correct, visible on the meters | −20…−36 dB = "vocal drop" automation | The silence is truth, not a bug; document in tooltip |
| **Tilt ±18 dB** | A full spectral seesaw — brilliance wall or felt-dark sludge. ±6 dB "would be the timid version" (DistortionEngine.h:333, verbatim) | ±10…18 with Pivot swept = a performable DJ-morph | Linear filter — unconditionally stable |
| **Mono Below 500 Hz** | Everything below 500 Hz forced center: the whole low-mid field snaps mono — drastic on wide pads, THE club-safe move on bass patches | 120-300 Hz = club/vinyl duty | Q = 0.7071 fixed, no resonance, no clamp needed |
| **Widen ±30 ms** | Past-textbook Haas: hard doubling at the edge of slapback; mono sum combs from 16.7 Hz spacing at full throw | 8-18 ms = huge fake stereo | The comb IS the trade — metered, not prevented |
| **Rotate ±45°** | The entire field lies on one diagonal — a mono source is hard-panned, a wide source folds through anti-phase | ±20-45 on wide material | Rotation matrix — lossless, no clamp |
| **Low Cut 1 kHz + High Cut 200 Hz** | Crossed cuts: the program collapses to a resonance-free whisper — a "telephone made of fog" | Either alone at max = surgical destruction | 12 dB/oct fixed slopes — stable |
| **Mix 100 %** | Fully wet (law 4). With Flip=Both and Mix 50 %: dry+inverted-wet **cancels to −∞** — a phase-blend comb tool, deliberate (§10) | Mix 40-60 under Flip = variable comb | Document; never "fix" |

**Taper laws:** Gain linear-in-dB (§3.1 — the ear hears dB); Width piecewise-linear with the unity
detent at half-travel (the whole bottom half is the mono→stereo story, the top half is the boost);
Mono Below / Pivot / cuts exponential in Hz (perceptually even sweeps — the octave law); Widen linear
in ms (Haas perception is roughly linear in τ); Rotate linear in degrees.

---

## 5. Chassis map — fb275, exactly

### 5.1 Front card — 3 + Mix (+ 2 pills)

| Control | Param | Range / taper | Default | Why it's front |
|---|---|---|---|---|
| **Gain** *(hero, biggest)* | `SYN_UTL_GAIN` | −∞ → +36 dB, §3.1 taper, unity detent | 0 dB | THE chain-leveling knob — the device's reason to exist |
| **Width** | `SYN_UTL_WIDTH` | 0-400 %, unity detent at center | 100 % | The imaging hero; drives the goniometer directly |
| **Tilt** | `SYN_UTL_TILT` | ±18 dB, center detent | 0 | The one voicing move; bold, visible, bipolar |
| **Mix** | `SYN_UTL_MIX` | equal-power, 100 % = fully wet | 100 % | House chassis law |
| pill **Mono** | `SYN_UTL_MONO` | bool, latching, 30 ms glide to width 0 + Route-Mono matrix | OFF | The one-click mix check (Ableton's headphone audition, made a latch per the state-persists law) |
| pill **DC** | `SYN_UTL_DC` | bool, 10 Hz blocker, 30 ms fade | OFF | Cleans upstream FM/fold/asym-distortion offset |

Front knobs are their own params (the distortion grammar: front Drive/SIG/Tone ≠ back-8).
All names pragmatic (say what it does): Gain, Width, Tilt, Mix, Mono, DC.

### 5.2 Back panel — 2 dropdowns + 8 knobs (4×2), 3 separators

**Dropdowns:** `Route` (§2.1, 6 entries) · `Flip` (§2.2, 4 entries). Real `<select>` overlays
(the `engine-select` idiom — dropdowns, never click-to-cycle).

| Slot | Knob | Param | Range / taper | Default | Glide |
|---|---|---|---|---|---|
| P1 | **Balance** | `SYN_UTL_BALANCE` | ±100 %, quarter-cos opposite-channel law (§3.2), center detent | 0 | 15 ms |
| P2 | **Center** | `SYN_UTL_CENTER` | −∞…+12 dB mid gain (§3.4), 0 dB detent | 0 dB | 15 ms |
| P3 | **Mono Below** | `SYN_UTL_MONOFREQ` | Off → 20-500 Hz side-HPF, exp (§3.4) | Off | 20 ms coeff + engage xfade |
| P4 | **Widen** | `SYN_UTL_WIDEN` | ±30 ms bipolar Haas (§3.7), 0 detent | 0 | dual-tap 15 ms xfade |
| P5 | **Low Cut** | `SYN_UTL_LOWCUT` | Off → 20-1000 Hz HP 12 dB/oct, exp | Off | block-rate slew |
| P6 | **High Cut** | `SYN_UTL_HICUT` | 200 Hz-20 kHz → Off LP 12 dB/oct, exp | Off | block-rate slew |
| P7 | **Pivot** | `SYN_UTL_PIVOT` | 150 Hz-1.5 kHz, exp; Tilt's hinge (§3.5) | ~700 Hz | block-rate slew |
| P8 | **Rotate** | `SYN_UTL_ROTATE` | ±45°, linear, 0 detent (§3.8) | 0 | 15 ms |

**Grid layout (4×2, zero dead space):**
Row 1: Balance · Center · Mono Below · Widen — *the image row*
Row 2: Low Cut · High Cut · Pivot · Rotate — *the tone + field row*

### 5.3 The full ID block (ParameterIDs.hpp grammar, mirrors SYN_DLY_/SYN_DST_)

```cpp
constexpr char SYN_UTL_ROUTE[]    = "SYN_UTL_ROUTE";     // choice(6): Stereo/Mono/Side/Left/Right/Swap
constexpr char SYN_UTL_FLIP[]     = "SYN_UTL_FLIP";      // choice(4): None/Left/Right/Both
constexpr char SYN_UTL_GAIN[]     = "SYN_UTL_GAIN";      // float 0..1 -> −inf..+36 dB (§3.1; unity detent)
constexpr char SYN_UTL_WIDTH[]    = "SYN_UTL_WIDTH";     // float 0..1 -> 0..400 % side gain (100 % at 0.5)
constexpr char SYN_UTL_TILT[]     = "SYN_UTL_TILT";      // float 0..1 -> ±18 dB dB-symmetric tilt (0.5 = flat)
constexpr char SYN_UTL_MIX[]      = "SYN_UTL_MIX";       // float 0..1 equal-power; 100 % = FULLY WET
constexpr char SYN_UTL_BALANCE[]  = "SYN_UTL_BALANCE";   // float 0..1 -> ±100 % opposite-channel atten
constexpr char SYN_UTL_CENTER[]   = "SYN_UTL_CENTER";    // float 0..1 -> −inf..+12 dB mid gain (0 dB at 0.5)
constexpr char SYN_UTL_MONOFREQ[] = "SYN_UTL_MONOFREQ";  // float 0..1 -> Off/20..500 Hz side-HPF
constexpr char SYN_UTL_WIDEN[]    = "SYN_UTL_WIDEN";     // float 0..1 -> ±30 ms Haas (sign = channel)
constexpr char SYN_UTL_LOWCUT[]   = "SYN_UTL_LOWCUT";    // float 0..1 -> Off/20..1000 Hz HP
constexpr char SYN_UTL_HICUT[]    = "SYN_UTL_HICUT";     // float 0..1 -> 200..20k Hz LP/Off
constexpr char SYN_UTL_PIVOT[]    = "SYN_UTL_PIVOT";     // float 0..1 -> 150..1500 Hz tilt hinge
constexpr char SYN_UTL_ROTATE[]   = "SYN_UTL_ROTATE";    // float 0..1 -> ±45° M/S rotation
constexpr char SYN_UTL_MONO[]     = "SYN_UTL_MONO";      // bool pill — latching mono check
constexpr char SYN_UTL_DC[]       = "SYN_UTL_DC";        // bool pill — 10 Hz DC blocker
constexpr char SYN_UTL_POWER[]    = "SYN_UTL_POWER";     // bool — device power, DEFAULT OFF (house)
constexpr char SYN_UTL_SRC_A..D/_SUB/_NOISE               // bools, per-osc routing — PENDING §12 Q2
constexpr char SYN_UTL_POS[]      = "SYN_UTL_POS";       // choice(2): First/Last — chain slot, §7.2
```

Choice params are read as **index** via `getRawParameterValue` (the CLAUDE.md §4 law — never
normalize). Every new param needs the 4-point WebSliderRelay chain or it silently no-ops.

---

## 6. Visualizers

### 6.1 How the greats draw this effect — mechanisms, precisely

* **Goniometer (classic/PSP/MSED).** L/R plotted on axes rotated 45°: x = S-ish, y = M-ish. A
  left-only signal draws the top-left↔bottom-right diagonal; right-only the opposite diagonal; a
  centered mono source a **vertical line**; a polarity-flipped channel a **horizontal line** (the
  instant "something is wrong" read). Persistence (CRT phosphor or digital trail) is essential —
  the envelope of the cloud is the object of interest, not single frames. MSED brands its variant
  the "Plasma Vector Scope."
* **Ozone Imager / Insight — three modes.** *Lissajous:* per-sample dots in Cartesian (the
  goniometer's raw form; stereo = taller-than-wide cloud, mono = vertical). *Polar Sample:* the same
  dots in polar coordinates (angle = position, radius = level) — reads better for width judgments.
  *Polar Level:* **rays** whose length = amplitude and angle = stereo position — the most "meter-like"
  and least CPU-cloudy. 45° "safe lines" overlay: energy inside = in-phase, outside = out-of-phase.
* **Correlation meter (the beis.de-correct math).** `c = LP(L·R) / √(LP(L²)·LP(R²) + ε)` with
  **three identical low-pass integrators** (~150-300 ms; Bekkr documents Pearson + ~150 ms EMA).
  +1 mono · 0 uncorrelated-wide · −1 anti-phase. beis.de's warning: the cheap zero-crossing/XOR
  "phase meter" is NOT a correlation meter (reads 100 % on signals that are ~42 % correlated) — build
  the real one, it is 3 one-poles and a sqrt per block.
* **Waves PAZ — Stereo Position Display.** A semicircle of energy vs pan angle, with anti-phase
  energy shown separately — energy-domain (loudness-modeled), not sample-domain. Intuitive but
  FFT-adjacent in cost; noted, not copied.
* **Kilohearts Stereo.** A tiny balance + correlation bar that goes **red below zero** — proof a
  meaningful meter fits in 20 px.
* **Serum 2 Utility: no visualizer at all** — the module is a knob strip. **This card is our
  category win.**

### 6.2 The −26 dBFS reality (house law 1, applied to pixels)

A goniometer naively scaled to full-scale shows our −26 dBFS program as a dot cloud at **5 % radius —
permanently tiny** = an automatic fail of the dramaticism law. The fix is split-brained and honest:
* The **cloud/shape** is normalized by a slow AGC envelope (attack instant, release ~2 s, the fb312
  peak-hold grammar) — shape, width, rotation always fill the card.
* The **L/R bars** show *true* dBFS with a tick at −26 ("program") and 0 (clip) — absolute level
  lives where absolute level belongs. Gain moves the bars; the cloud stays framed.

### 6.3 Concept A — **The Field** (recommended)

Square canvas core (the fx-rack v7 core slot). One 2D canvas, one rAF pass, zero shadowBlur (fb342/343
law — layered strokes for glow, never filters):

* **Dot-cloud goniometer** (Lissajous, 45°-rotated): 128-256 decimated post-device sample pairs per
  frame, drawn as 1-2 px dots; persistence via one `fillRect` of translucent background per frame
  (the classic trail trick — ~zero cost). Idle = dim floor glow; playing = bright cloud (law 9's
  idle/bright delta).
* **Param reflection, every knob:** Width scales the cloud horizontally (S axis) in real time; Rotate
  rotates the whole cloud (the knob IS the image); Flip L/R flips the cloud to horizontal; Balance
  slides the cloud's centroid; Mono pill collapses it to the vertical line live; Mono Below draws a
  faint horizontal "floor band" at the low-frequency zone whose cloud is forced narrow; Tilt tints
  the dot color temperature warm↔cool (dark = −, bright = +); Gain scales dot brightness + the bars.
* **Correlation arc** along the bottom edge: −1…+1, white tick, the **negative half draws in the
  warning color** (Kilohearts red-zone lineage; purple-on-select stays the house accent).
* **L/R peak bars** hugging left/right edges: true-dBFS, instant-attack/0.05-fall ballistics — the
  exact `dstBloomViz_` publish pattern (`PluginProcessor.cpp:7393-7407`), extended to a small packed
  struct.
* 45° **safe lines** as 1 px hairlines (Ozone convention), visible only while playing.

### 6.4 Concept B — **Polar Rays** (alt)

Polar-Level style: 64 rays, angle = pan position, length = short-window band energy — cheaper than
dots at high densities, reads like a meter, rotates/scales with Rotate/Width identically. Less
"alive" than the cloud; better at showing Balance asymmetry. Could be the second view behind small
corner tabs (`Cloud | Rays`) — a view gesture on the card, not a dropdown param.

### 6.5 Concept C — **Strip Meters** (fallback/minimal)

L/R + M/S bar quartet + correlation bar + gain-reduction-style width readout. Cheapest, information-
dense, zero drama — ships only as the collapsed/mini-card state, never the hero view.

**Data path (the fb90/fb232 laws):** one native fn `getUtilityViz` returning the packed frame
{128 pairs, corr, pkL, pkR, midRms, sideRms}; UI paints from cache when the JUCE promise never
settles; the popped card rides the 60 Hz push lane, self-poll demoted to 500 ms reconcile; `__cardOnly`
guards on the rack rAF driver. Decimation ×4 in the audio thread (write every 4th sample pair into a
lock-free ring; the block publishes the read index atomically).

---

## 7. Interplay — the chain story

### 7.1 Unity-through: the null gate

At defaults (Route Stereo, Flip None, Gain 0 dB detent, Width 100 %, Tilt 0, Balance 0, Center 0 dB,
Mono Below Off, Widen 0, cuts Off, Rotate 0, pills off, Mix 100 %) the device is **bit-transparent**:
out − in ≡ 0.0 (not −60 dB — zero; every default multiplies by exactly 1.0 and no filter is in-path
when Off). This is the harness gate #1 and the reason the Balance law and Off-bypasses are specified
the way they are. A leveling tool that colors at rest cannot be trusted to level.

### 7.2 Position in the chain — and the 24-permutation problem

`SYN_FX_ORDER` is a 6-way permutation of 3 devices (`PluginProcessor.cpp:3488`, switch `:7383`). A
4th device makes 4! = 24 permutations — an unusable dropdown and a param-migration headache.
**Proposal: do NOT extend the permutation.** Utility gets its own 2-state slot param `SYN_UTL_POS`:

* **First** (default): before the permuted trio — the *input-leveling* role: Gain = drive control for
  whatever device comes first (the free extra drive stage for the distortion), cuts/DC clean the feed,
  Mono Below disciplines what the reverb widens.
* **Last**: after the trio — the *output/imaging* role: trim the summed wet, widen/rotate the final
  field, bass-mono the reverb's stereo lows, tilt the whole rack's voice before `kInstrumentMakeup`
  (`:7414`) and the fb264 limiter.

Two positions cover the two real jobs; the 6-way trio dropdown stays untouched; legacy sessions
restore exactly. (Open question §12-1: Max may instead want Utility INSIDE the permutation — the cost
is the 24-entry list.)

### 7.3 What it does to the chain downstream

* **Into Distortion:** +N dB of Utility gain ≈ +N dB of extra Drive on every mode (the drive laws are
  input-referred) — but *without* moving the distortion's own knob mappings; the extremity tables in
  the distortion bible assume the −26 dBFS bus, so document that chain-first Utility gain shifts every
  "dangerous zone" down by the same dB.
* **Into Reverb/Delay:** tilt/cuts pre-space = the classic "EQ the send, not the return" wisdom; Mono
  Below pre-reverb narrows the excitation, post-reverb narrows the tail — audibly different, both
  valid, the First/Last toggle IS this choice.
* **Width stacking multiplies:** Delay Width (×1.6 max, `DelayEngine.h:92`) × Utility 400 % = ×6.4
  side gain — correlation goes hard negative. Not clamped (law 5); the meter is the contract.
* **Mix discipline:** the send/exclusion grammar means a 4th send bus must join EVERY main-send
  exclusion sum — `PluginProcessor.cpp:7159/:7161`, `:7326/:7328`, `:7358/:7360` (the fb305/fb338
  landmine, verbatim in the code comments) plus the block-setup pair at `:6979/:7111`. If Utility
  ships **main-insert only** (recommended v1, §12-2), none of that is touched.

### 7.4 Classic ordering wisdom (for the manual/tooltips)

Gain staging first; corrective moves (DC, cuts, flips) before creative devices; imaging last;
bass-mono as the final imaging op (§3.0's internal order restates chain wisdom in miniature). When
stacked with itself in the future node-chain era: two Utilities (chain-in trim + chain-out imaging)
is the professional pattern, not a smell.

---

## 8. Presets — 13 factory sketches

| # | Name | Intent | Rough values (params not listed = default) |
|---|---|---|---|
| 1 | **Wire** | The init: bit-transparent, meters on | all defaults |
| 2 | **Club Sub Lock** | Mono-below-120 club/PA safety | MonoBelow 120 Hz · Width 115 % · LowCut 24 Hz |
| 3 | **Vinyl Safe** | Lathe-friendly master feed | MonoBelow 300 Hz · HighCut 16 k · Width 90 % |
| 4 | **Mono Maker** | Full glidable collapse | Width 0 % · Gain +2 dB |
| 5 | **Phone Check** | Small-speaker reality check | Route Mono · LowCut 320 Hz · HighCut 3.4 k · Gain −6 |
| 6 | **Karaoke Drop** | Kill the center, keep the wash | Center −∞ · Width 200 % |
| 7 | **Side Stage** | Recessed mid, huge sides, safe lows | Center −12 dB · Width 250 % · MonoBelow 150 Hz |
| 8 | **Haas Double** | Fake-double a mono lead | Widen +18 ms · Width 130 % · MonoBelow 200 Hz |
| 9 | **Ultra Wide** | The 400 % showpiece | Width 320 % · MonoBelow 140 Hz · Tilt +3 dB |
| 10 | **Air Push** | Bright tilt voicing | Tilt +9 dB · Pivot 800 Hz |
| 11 | **Seventies Glue** | Dark tilt voicing | Tilt −7 dB · Pivot 500 Hz · Width 85 % |
| 12 | **Feeder** | Slam the next device (chain-first) | Gain +18 dB · Pos First |
| 13 | **Rotor Pad** | Rotated, widened, doubled pad field | Rotate +30° · Width 160 % · Widen −8 ms |

Preset infra: the fb342 `.fxr-preset` grammar (pid `utl_<slug>`), factory table beside `DST_PRESETS`.
Level discipline: every preset's output within ±3 dB of bypass on the standard 3-note chord probe
(the Phase-G preset-level-spread lesson — no Gargle-+28 outliers).

---

## 9. CPU — budget and tiering

* **Audio path:** matrix 4 mul + gains ~6 + tilt 4 + 2 SVFs ~10 + side-HPF ~5 + rotate 4 + Haas read
  (cubic) ~8 + mix 4 ≈ **45-70 flops/sample stereo** worst-case, ~15 when Off-paths are bypassed.
  ≈ **0.1-0.3 % of one core @48 k** — the cheapest device in the rack by ~an order of magnitude.
* **Meters:** correlation = 3 one-poles + 1 sqrt per **block**; peaks = 2 compares/sample; gonio ring
  write = 1 store per 4 samples. Negligible.
* **Tiering: none.** No Quality dropdown, no oversampling (§3.10 — linear device, nothing to
  alias). What must NEVER be oversampled: all of it.
* **Sleep:** the awake-head pattern (fb342) — when input is silent (< −90 dBFS for ~0.5 s) skip the
  filters and meters, publish idle-frames; wake instantly on signal.
* **UI:** canvas dots ≤ 256/frame, one trail fillRect, zero shadowBlur/filters (fb342/343), rAF
  `__cardOnly`-guarded, 60 Hz push lane not self-poll.

---

## 10. Pitfalls — the collected traps

1. **Zipper on Gain** — the #1 utility-plugin sin (Airwindows built a whole plugin point around it).
   Every continuous param glides (§3.9); Gain glide on LINEAR value.
2. **Haas mono comb** — τ = 10 ms nulls at 50/150/250… Hz. Not preventable, must be *visible*:
   correlation bar + Mono pill are the disclosure. Never auto-"fix" with allpasses.
3. **Mix × Flip cancellation** — Mix 50 % + Flip Both = −∞ by math. Intended (phase-blend tool);
   tooltip it; never special-case it away. Default Flip None keeps naive users safe.
4. **Mid-kill on mono sources = silence** — Center −∞ or Route Side on a mono patch outputs nothing
   (there is no side). Truth, not a bug; meters show side = −∞ so the *why* is on screen. (The fb325
   lesson: on a mono source there IS no side — never build a knob whose whole travel needs S ≠ 0
   without showing S.)
5. **Filter-engage snap** — a biquad entering the path with zeroed state clicks; crossfade in (§3.4)
   and slew coefficients, never swap them (the param-stale/F-cache crackle class from Phase G).
6. **Route-switch through glided matrices** passes intermediate mixes (Stereo→Swap sweeps through a
   mono-ish middle) — benign and click-free, but the gonio will show the morph; that is a feature.
7. **Denormals** in SVF/one-pole tails — `ScopedNoDenormals` + flush; no feedback so no bloom, but
   the Haas buffer of a decayed note still ticks denormal reads without FTZ.
8. **The fb305/fb338 send landmine** — if Utility ever gets per-osc SRC pills, `utlSend` must join
   every exclusion sum at `PluginProcessor.cpp:7159/:7161/:7326/:7328/:7358/:7360` AND the setup pair
   at `:6979/:7111`, or Mix-100 dry-removal breaks for every OTHER device. v1 recommendation:
   main-insert only, no send bus (§12-2).
9. **24-permutation trap** — do not extend `SYN_FX_ORDER`; use `SYN_UTL_POS` First/Last (§7.2).
10. **Goniometer autoscale lying about level** — AGC-normalized cloud + true-dB bars split (§6.2);
    never AGC the bars.
11. **Meter native promise never settles** — paint from cache (the JUCE-promise house law); 500 ms
    reconcile poll only.
12. **Choice params read as index** — `(int)*rawParam(SYN_UTL_ROUTE)`, never `lround(raw·N)` (the
    fb50 class).
13. **Detent params defaulting to extremes** — Width/Tilt/Balance/Center/Widen/Rotate all default to
    their CENTER detents (the "prefer 0.5 defaults" law; double-click reset returns to detent, not 0).
14. **Bypass ≠ silence** — `SYN_UTL_POWER` OFF must be a true insert bypass (the power-gates-
    everything law), fade-swapped 30 ms, and MUST also bypass the meters' *published activity* (idle
    frames) so the card dims (law 9's idle state).
15. **Don't oversample it** — someone will suggest it "for quality." There is nothing to oversample
    (§3.10); it would only add latency risk and the fb305-class latency maths says any device latency
    must be compensated-internal and reported zero.

---

## 11. Hard-rule compliance checklist (laws 1-10, walked)

1. **Bus reality (−26 dBFS):** Gain ceiling +36 dB chosen to cross full scale from −26 (§3.1); viz
   AGC/true-bar split (§6.2); preset levels probed on the real bus (§8). No literature range copied
   raw (Ableton's ±35 informs but the taper is house-law `dB-linear`, unity-detent).
2. **Chassis fb275:** front 3 + Mix + 2 pills; back exactly 2 dropdowns (Route, Flip) + 8 knobs 4×2
   (§5.2); pragmatic Title-case names throughout; dropdowns are real selects.
3. **Time params 4 bars→1/256:** N/A — Widen (±30 ms) is a psychoacoustic micro-delay, not a
   tempo-relevant echo; it must NOT sync (syncing 30 ms is meaningless). Stated so the rule is
   consciously inapplicable, not forgotten.
4. **Mix 100 % = fully wet** (§3.9); Route/Flip switches glide-morph, never cut (§3.9).
5. **Params evolve 0→100:** every taper specified with no plateaus (§4 taper laws); conditional pairs
   (Tilt+Pivot) follow the sanctioned matched-pair law; no fake Types — the dropdowns' every entry
   carries a stated measurable discriminator (§2).
6. **Nothing free-runs:** no feedback, no oscillator, no noise source in the device; output dies with
   input by construction; loop gain N/A (§3.0).
7. **No clicks:** glide table §3.9 covers every param class incl. matrices, coefficients, delay taps,
   pills, power.
8. **CPU-friendly:** 0.1-0.3 % core, no oversampling ever, sleep-when-silent (§9).
9. **Audible ⇒ visible + dramatic:** every one of the 14 sound-changing controls has a named viz
   reaction (§6.3); idle-dim/playing-bright specified; the −26 dB smallness defeated by design (§6.2).
10. **Recycle first (verified by reading):** DCBlocker `TerrainFilters.h:69`; TPTOnePole `:83`;
    SvfMultimode `:317`; M/S wet-width idiom `DelayEngine.h:208-210`; output tilt `DelayEngine.h:
    262-263`; dB-symmetric emphasis + "±6 dB is timid" `DistortionEngine.h:333/:630`; DC r-law
    `DistortionEngine.h:124-125`; equal-power mix `PluginProcessor.cpp:7112`; bloom-viz publish
    `:7393-7407`; preset infra fb342; fx-rack v7 chassis + `engine-select` + `.pmenu`. New code is
    limited to: the 2×2 glided matrix, the side-HPF wrapper, the Haas dual-tap, the rotation, and
    the viz frame packer.

---

## 12. Open questions for Max

1. **Chain slot:** First/Last toggle (`SYN_UTL_POS`, recommended — keeps `SYN_FX_ORDER` at 6) vs
   folding Utility into a 24-entry permutation? (§7.2)
2. **Per-osc SRC pills for Utility?** A 4th send bus costs the full fb305/fb338 exclusion-sum surgery
   (§10-8). Recommendation: v1 = main-insert only; pills arrive with the node-chain epic.
3. **Dropdown 2:** `Flip` (recommended, sound-changing) — or a Scope view selector with Flip demoted
   to pills? (Current design: views = corner tabs on the card, §6.4.)
4. **Front trio:** Gain · Width · Tilt confirmed? (Alternative: Balance in Tilt's seat and Tilt on
   the back — but Tilt is the drama knob.)
5. **Tilt reach ±18 dB** (house emphasis pair) or a tamer ±12 for the "clean tool" identity?
6. **Mono pill: latch** (state-persists law, current spec) — or momentary-while-held audition like
   Ableton's headphone icon? Or both (click = latch, hold = momentary)?
7. **Widen bipolar ±30 ms** with sign = channel — confirmed over a separate channel selector?
8. **Does Utility deserve the 6th device-viz slot polish pass** (The Field concept A) now, or ship
   Strip Meters first and upgrade with the chain epic?

---

## 13. Sources

* Serum 2 User Guide (local PDF), "Using Serum FX → Utility", p.182 — the competitor param set.
* Ableton Live manual — Live Audio Effect Reference (Utility): https://www.ableton.com/en/live-manual/12/live-audio-effect-reference/
* Cycling '74 `abl.device.utility~` reference (Ableton Utility port; exact attribute ranges): https://docs.cycling74.com/reference/abl.device.utility~
* Kilohearts Stereo snapin: https://kilohearts.com/products/stereo
* Kilohearts Haas snapin: https://kilohearts.com/products/haas
* Voxengo MSED (M/S encoder/decoder, inline mid/side gain/pan/mute, Plasma Vector Scope): https://www.voxengo.com/product/msed/
* Airwindows PurestGain (gain-smoothing/zipper doctrine): https://www.airwindows.com/purestgain/
* iZotope Ozone Imager (Width, Stereoize I/II, three vectorscope modes): https://www.izotope.com/en/products/ozone-imager.html
* Eberhard Beis, "False and Correct Audio Correlation Measurements" (the correct meter math): http://www.beis.de/Elektronik/Correlation/CorrelationCorrectAndWrong.html
* Wikipedia — Goniometer (audio) (axes, patterns, persistence): https://en.wikipedia.org/wiki/Goniometer_(audio)
* Wikipedia — Panning law (−3/−4.5/−6 dB variants): https://en.wikipedia.org/wiki/Panning_law
* KA Electronics — Elliptic Equalizer (side-HPF vinyl lineage, 6 dB/oct historic): http://ka-electronics.com/kaelectronics/Elliptic_EQ/Elliptic_EQ.htm
* ASMR Education — side-channel HPF in M/S mastering: https://asmr.education/faq/music-mastering/high-pass-filter-side-channel-mid-side-mix
* FaderPro — Treating Low Frequencies for the Club: https://blog.faderpro.com/mixing/treating-low-frequencies-for-the-club/
* Flo Town Mastering — Center That Sub (mono low-end guide): https://flotownmastering.com/blog/center-that-sub
* Softube — Tonelux Tilt manual (tilt topology, pivot ~600 Hz-1 kHz, ±10-12 dB extremes): https://www.softube.com/user-manuals/tonelux-tilt-and-tilt-live
* Waves PAZ manual — Stereo Position Display mechanism: https://www.manualslib.com/manual/188619/Waves-Psychoacoustic-Analyzer-Paz.html
* Bekkr correlation meter doc (Pearson + ~150 ms EMA convention): https://zsazsaroboto.com/docs/bekkr/components/master/corr/
* Repo (all verified this session): `PluginProcessor.cpp` :46/:3488/:6300/:7112/:7159/:7326/:7358/:7383/:7393-7407/:7414 · `DelayEngine.h` :92/:208-210/:262-263 · `DistortionEngine.h` :124-125/:333/:528-549/:630/:965/:2706 · `TerrainFilters.h` :69/:83/:317 · `ParameterIDs.hpp` :374-401/:406-431.
