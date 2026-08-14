# Terrain Instrument — Equalizer Build Bible

**v1 — research complete. The single authoritative spec for the 4th FX device.**
Written after fb345 (Phase G distortion certification closed). Same structure, same laws, same
chassis as `DISTORTION-BUILD-BIBLE.md` / `REVERB-BUILD-BIBLE.md`. No DSP written yet.
Every repo line number below was read on 2026-08-14; **`index.html` numbers drift with every UI
build — re-grep the symbol, don't trust the number.** C++ refs have been stable.

> **The one-sentence pitch:** Serum 2's Equalizer is a two-band afterthought (Low/High, each
> Shelf/Peak/Filter, six knobs, a tiny curve). Ours is a four-band, seven-personality device with
> decramped top-octave math, a Mid/Side focus switch, one dynamic personality, and the Pro-Q-grammar
> visualizer **that is already built and shipping in this repo**. Width matched, depth beaten — again.

---

## 0. Scope + THE REPO AUDIT (read this before anything)

### 0.1 What already exists — the v6 synth-panel EQ (months old, still shipping)

Terrain has TWO EQ surfaces today and it is critical not to confuse them:

| | The synth-panel EQ (exists) | The Equalizer DEVICE (this bible) |
|---|---|---|
| Where | SYN/EQ/DLY/MOD panel switcher, `#eq-panel` `index.html:5526` | The FX rack, 4th device card after Reverb/Delay/Distortion |
| Params | `EQ_B1..B7` + HP/LP (`ParameterIDs.hpp:40-61`) — 7 bells + edge cuts, 37 params (35 audio + 2 UI-hint mode flags) | `SYN_EQZ_*` — 11 knobs + 2 dropdowns + Mix, fb275 chassis |
| Engine | `ParametricEQ.h` (header-only, per channel) | **NEW** `EqualizerEngine.h` (this spec) |
| Role | Master corrective EQ on the whole instrument | A routable, characterful DEVICE (per-osc pills + main send) |

**The v6 engine audit — `Source/ParametricEQ.h`, read in full:**

* Topology (`:13-18`): serial `HP(1/2/4 biquads Butterworth) → B1..B7 peaking → LP(1/2/4)`. All
  `juce::dsp::IIR` RBJ-cookbook filters.
* **🔑 THE DISQUALIFIER:** `updateAllCoefficients()` (`:135-158`) runs **once per sample** from
  `processSample` (`:104`), and every call does
  `*bandFilters[b].coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(...)` for all
  7 bands plus up to 8 HP/LP stages. `makePeakFilter` **heap-allocates a new ReferenceCountedObject
  every call** — that is 9-15 audio-thread allocations *per sample per channel*, plus the full
  trig design math (sin/cos/pow) at audio rate. It survives on the synth panel because it is one
  instance and modern allocators are fast, but it is **not** the pattern for a rack device that must
  live in the future multi-device Patcher chain. The comment at `:103` ("cheap if no change
  targeted") is wrong — there is no dirty-flag; it recomputes unconditionally.
* **🔑 THE NaN TRAP (keep this comment alive):** `:42-47` documents that smoothers default to 0, so
  Q=0 → `alpha = sin/(2*Q) = NaN` → coefficients poisoned **for the lifetime of the instance**.
  The fix (seed smoothers via `setCurrentAndTargetValue` before first design) must be carried into
  the new engine verbatim.
* Good bones to carry forward: 5 ms `LinearSmoothedValue` on freq/gain/Q (`:50-52`), the
  `isfinite` output guard (`:124`), the **Solo** grammar (`setSolo` `:97`, band-pass-the-INPUT
  audition at `:174-191` — this is Ableton EQ8's headphone-audition, already invented in-tree),
  and the smoother-burn idiom during solo so release doesn't jump (`:180-186`).
* Processor side: smoothers declared `PluginProcessor.h:1623`; APVTS layout `PluginProcessor.cpp:1343-1359`;
  prepare `:3858-3868`; block-rate target push `:6413-6420`; **EQ freq/gain are already MOD
  DESTINATIONS** (`ModulationEngine::pEqB1Freq + b*3` read per-sample at `:6853`).

### 0.2 The UI audit — the Pro-Q grammar is ALREADY BUILT

This is the single biggest recycle in the project. The synth-panel EQ ships:

* `#eq-canvas` + `#eq-handles` overlay (`index.html:3461-3465`, `:5534-5535`).
* `redrawEqCanvas()` at `:18335` — draws the summed response curve **and a live pre/post spectrum
  with a −80 dB floor** (comment at `:18372`).
* `redrawEqHandles()` at `:18422` — **draggable band nodes**: mousedown/drag = freq+gain
  (`:18614-18632`), **wheel = Q** (`:18668`), right-click context menu (`:18693`), dblclick reset
  (`:18833`), tooltip with auto-hide (`:18686-18687`).
* The C++ feed: `SpectrumAnalyzer.h` — 4096 FFT, 75 % overlap ≈ 47 frames/s, triple-buffered
  lock-free (`readLatest()` never blocks). Two instances `analyzerPre, analyzerPost` at
  `PluginProcessor.h:924`, pushed at `PluginProcessor.cpp:6865-6869` **gated by `vizLive`** (they
  cost nothing when no EQ surface is open).
* The dead-feed fade law (fb156/fb66, `index.html:25108-25111`): at silence the spectrum rides the
  input level to zero ink — idle = dim is already implemented, not a new requirement.

**VERDICT — the device-ification rework scope:**

1. **UI: recycle wholesale.** The device card's core visual is the eq-canvas/eq-handles pair,
   re-instantiated inside the fx-rack card SVG (`.fxr-core` grammar), fed by the same two
   analyzers re-tapped around the DEVICE instead of the panel EQ (§6.3). Do not write a new
   drag system. Namespace everything `eqz*` (namespace-or-collide law, fb266).
2. **Engine: rebuild small.** A new `EqualizerEngine.h` (~350 lines) with block-rate filter design,
   per-sample coefficient ramps, Simper-SVF and matched-biquad cores (§3). The v6 engine keeps
   serving the synth panel untouched; backporting the new core to it is a separate, later commit.
3. **Params: new family `SYN_EQZ_*`.** Do NOT reuse `EQ_B*` ids — the two surfaces must stay
   independently automatable and preset-able (no-doubles applies to param IDs too).

---

## 1. History and circuits — the lineage that defined the effect

EQ is the oldest effect there is, and every "Type" in §2 is a real branch of this tree:

* **1930s-40s — program equalizers for film.** Western Electric / Langevin passive LC networks with
  fixed curves, gain made up by the console. The word "program" meant: gentle, wide, meant for the
  whole mix.
* **1950s — Pultec EQP-1 → EQP-1A** (Eugene Shenk / Ollie Summerlin, RCA Institute classmates;
  Pulse Techniques incorporated 1953, EQP-1 introduced 1956, the EQP-1A refinement 1961 — the
  folklore "1951" date is the prototype era). Passive LC boost + *separate* RC cut
  sections with a tube (12AX7/12AU7) makeup stage. Low boost shelf at **20/30/60/100 CPS, 0-13.5 dB**;
  low atten at the same points, **0-17.5 dB**; high boost is a **peak** (not shelf) at
  **3/4/5/8/10/12/16 kc, 0-18 dB with a Bandwidth (Q) knob**; high atten shelf at **5/10/20 kc,
  0-16 dB**. Because boost and cut are *different circuits at slightly different corner
  frequencies*, engaging both at once creates the famous composite: **a big low hump with a dip
  just above it** (boost 30-60 Hz + atten → hump ~80 Hz, scoop ~200-400 Hz). That interaction —
  impossible on an ideal parametric — is the entire reason Pultec emulations still sell in 2026.
* **1952 — Peter Baxandall's tone control** (Wireless World). One feedback network, two knobs,
  gentle first-order shelves that overlap around a pivot — the ancestor of every Bass/Treble knob
  and of the one-knob **Tilt** EQ (the Tonelux Tilt revived it as a single seesaw knob around a
  fixed mid pivot).
* **Late 1960s — API 550 (Saul Walker): PROPORTIONAL Q.** At 2 dB the bell is broad and gentle; at
  12 dB it tightens sharply. One knob feels "musical" at every position because bandwidth is a
  *function of gain*. This is the single most load-bearing idea for our chassis (§2.2·3, §5): **Q
  does not need its own knob if Q is a law.**
* **1970 — Neve 1073.** Hand-wound inductor bells + fixed **12 kHz ±16 dB shelf** (AMS Neve's
  own spec: "smooth ±16 dB fixed frequency shelving at 12 kHz"). Inductor L-C
  sections ring slightly and the shelves under/overshoot before the rise — the measurable
  "British" signature.
* **1972 — George Massenburg, "Parametric Equalization" (AES 42nd Convention preprint).**
  Sweep-tunable op-amp EQ with *independently continuously variable* frequency, bandwidth and
  amplitude — coins the word "parametric". ⚠️ Attribution precision (from GM's own account,
  massenburg.com GM_ParaEQ.pdf): his topologies were **single-op-amp "T"-filter designs** — he
  explicitly disclaims the 4-op-amp state-variable-filter parametric that others built later; do
  not credit him with the SVF topology. Our Surgical type is his direct descendant in *interface*
  (the three independent parameters), realized on modern matched biquads.
* **1983 — SSL 4000E "black knob" 242 EQ** (developed with George Martin): the aggressive,
  forward console EQ; the later G-series 292 went proportional-Q and gentler. Console EQ =
  personality through the Q-law, again.
* **Broadcast Germany — Siemens W295b / Sitral** (the Soundtoys Sie-Q and Arturia SITRAL-295
  source): 3 bands, stepped, famous for a high band you can boost absurdly without harshness.
* **Maag EQ4 AIR BAND (from the NTI EQ3):** a boost-only shelf selectable up to **40 kHz** — most
  of the transition lies at/above audibility, so what remains in-band is a slow, gentle rise:
  brightness without sibilance. **This is Max's requested airiness, and it is a decramping problem
  by definition** (a 40 kHz analog corner does not exist at fs=44.1k unless you match it).
* **Digital era.** RBJ's *Audio EQ Cookbook* biquads (bilinear transform) became the lingua franca —
  and with them **cramping**: warping compresses the analog 0-∞ Hz axis into 0-Nyquist, so any bell
  above ~5 kHz at 44.1 k goes asymmetric and squashed, and every response is forced to a horizontal
  tangent at Nyquist. Fixes, in order: **Orfanidis 1997** (prescribe the Nyquist-frequency gain from
  the analog prototype — closed form), **Massberg 2011** (pre-warped analog-matched shelf/LP
  prototypes), **Vicanek 2016** (poles by impulse invariance + closed-form numerator fit at DC / f0 /
  Nyquist — cheapest, and the one we adopt §3.3), and brute-force oversampling (Ableton EQ8
  "Hi-Quality" runs the filters at 2×; we reject this — §3.6).
* **The modern bar.** FabFilter Pro-Q 3/4 (24 bands, dynamic-per-band, Natural Phase = decramped,
  the definitive visualizer grammar — §6.1), Kirchhoff-EQ ("Robust Nyquist-matched Transform",
  psychoacoustic adaptive topologies, 32 bands, 30 vintage models), TDR Nova (free, dynamic,
  the threshold-arc visual grammar), Kilohearts Slice EQ (6 filter types × 32 bands, L/R + M/S).
  **Serum 2's Equalizer:** the compact 2-band EQ carried from Serum 1 — Low and High band, each
  switchable **Shelf / Peak / Filter**, each with Freq / Gain / Q, drawn as a small response curve
  with two draggable dots. Six knobs, no analyzer, no M/S, no dynamics. That is the bar to clear,
  and it is low.

---

## 2. The Types — seven personalities, one chassis

One device, name **`Equalizer`**. Same fixed four-band skeleton every Type (so the card, the
nodes, and muscle memory never change); the Type changes the **curve mathematics, the Q law, the
band interactions and the signature knob** — night-and-day by measurement, not by marketing.

The four bands (fixed roles, always the same knobs — §5):

```
LOW   low shelf     30–450 Hz      BODY  bell   120 Hz–2.5 kHz
BITE  bell          800 Hz–12 kHz  AIR   high shelf, analog corner 8–40 kHz (decramped)
```

### 2.1 The roster

| # | Type | Lineage | One-line recipe | 🔑 Measurable discriminator (the law-5 gate) |
|---|---|---|---|---|
| 1 | **Surgical** | Massenburg / Pro-Q | Constant-Q matched biquads, exact dB, zero color | Bell at 16 kHz, fs 44.1 k: response error vs analog prototype **< 0.5 dB** (RBJ shows > 3 dB cramp error); Q measured constant across ±18 dB |
| 2 | **British** | Neve 1073 / inductor | Broad bells (Q 1.0 fixed), shelves with deliberate undershoot | Low-shelf +12 dB shows a **−1 to −2 dB undershoot notch at ~0.35×fc** (shelf S ≈ 1.5, the Bump center); Surgical shows none |
| 3 | **American** | API 550 proportional Q | Q is a function of |gain| | −3 dB bandwidth at +2 dB boost ≥ **2.5×** the bandwidth at +12 dB boost, same knob |
| 4 | **Passive** | Pultec EQP-1A | Boost knob also engages a shifted cut; high band is a peak with wide fixed Q | Low +6 dB @60 Hz with Dip 60 % → measured **−2.5 to −4 dB trough at 200–400 Hz** that no other Type produces |
| 5 | **Air** | Maag EQ4 / Sie-Q | Everything widens (bell Q 0.35–0.5), Air shelf corner rides to 40 kHz | Air +10 dB: in-band slope **< 3 dB/oct below 20 kHz** while the matched analog prototype reaches full gain only above Nyquist; bells measure −3 dB bandwidth **> 3 octaves** |
| 6 | **Dynamic** | TDR Nova / Pro-Q 3 dynamic | Band gains fade in/out with the per-band envelope vs a program-calibrated threshold | The family tell, EQ edition: probe at −40 / −26 / −12 dBFS → magnitude response at the band moves **≥ 6 dB** across the span (all other Types: 0 dB by construction) |
| 7 | **Sculpt** | Digital-only, no ancestor | Gains ×1.67 (±30 dB), Ring drives Q 2→40, deep bells morph toward notches | Impulse through +24 dB Q 40 bell rings with **T60 > 60 ms** (2.2·Q/f); notch depth at min gain **< −40 dB** |

Seven, not ten — the research killed the near-twins before they shipped: an *SSL* type is
American-with-different-numbers (fails night-and-day vs #3), a *Tilt* type is redundant because
Tilt is a permanent front hero knob (§5), and a *Graphic* type is a band-count fantasy on a 4-band
chassis. (Distortion learned this the hard way — Phase G flagged D1 Si/Ge/Schottky as near-twins
*after* building them. We cut at the spec stage instead.)

### 2.2 Per-type spec blocks

#### 1 · Surgical — the reference type (default)

* All four bands = **Vicanek-matched** peaking / 2-pole shelf biquads (§3.3). Constant Q:
  bells 1.0, shelves S = 1.0 (no overshoot). Gains exact to the knob (±0.1 dB).
* Signature knob = **`Width`**: global Q multiplier 0.4×–4.0× (log taper, center = 1.0×).
  At 4× a +18 dB Body boost is already a playable resonance — the bridge toward Sculpt.
* This is the null-test type: all gains 0 ⇒ coefficients are identity (b=[1,0,0], a=[1,0,0]) ⇒
  **bit-exact pass-through** (§4.4).

#### 2 · British — the inductor console

* Bells Q fixed 1.0 (broad, "big-knob" feel), gain law softened `g_eff = g·(1−0.1·|g|/18)` so the
  top of the knob compresses like an inductor saturating its swing — still reaches ±16.2 dB.
* Shelves ride the Bump knob (default = center ⇒ S ≈ 1.5, Q ≈ 1.1 on the 2-pole shelf) ⇒ the
  documented under/overshoot dip before the rise. That dip is THE sound of a 1073 low shelf; do
  not "fix" it.
* Air band snaps its default Reach toward 12 kHz (the 1073 fixed shelf) but stays sweepable.
* Signature = **`Bump`**: scales the shelf S from 1.0 (flat, polite) to 2.0 (−3 dB undershoot,
  full vintage bloom). Measured discriminator lives on this knob.
* **NO saturation.** Distortion is another device (no-doubles for sounds, not just names). The
  British color here is 100 % curve shape.

#### 3 · American — proportional Q

* Bells: `Q(g) = 0.55 + 2.65·(|g|/18)^1.5` → Q 0.55 wide at 0 dB, ≈ 3.2 tight at ±18 dB
  (matches the measured API behavior: wide at 2 dB, narrow at 12).
* Shelves stay S = 1.0; Low band becomes a **bell below 80 Hz**? No — roles never change (chassis
  law); Low stays a shelf with proportional S: `S(g) = 1 + 0.5·(|g|/18)`.
* Signature = **`Grip`**: scales the proportional exponent 1.0→2.5 — at max, small moves are
  invisible and big moves are lasers; at min it degenerates toward Surgical-constant (but Q floor
  0.55 keeps it distinct).

#### 4 · Passive — the Pultec machine

* **Low band** (the trick, baked in): Gain > 0 engages the boost shelf at fc *plus* an attenuation
  shelf at `2.2×fc` with gain `−0.7·g·Dip`. Gain < 0 = pure atten shelf (the 17.5 dB direction —
  atten is allowed deeper: `g·1.15`). Result: the hump-plus-scoop composite from one knob.
* **Air band** = the EQP high *attenuator*: pure wide shelf cut/boost, S = 0.8 (slow).
* **Bite band** = the EQP high **peak boost**: Q fixed 0.7 broad (the Bandwidth knob at wide);
  the Q ignores Width laws — passivity means the curve is what the circuit gives you.
* Body = normal broad bell Q 0.8 (the EQM-1A mid section, so the type isn't two dead knobs).
* Signature = **`Dip`** 0–100 %: the atten ride-along amount. 0 % = modern clean boost;
  100 % = `−g·0.7` full trick. THE preset knob for kick/bass.

#### 5 · Air — the opener

* All bells widen: Q 0.35–0.5; gains map through a softener `g_eff = 18·tanh(g/14)` so extreme
  boosts stay silky (this is why Sie-Q "feels perfect no matter how much you boost" — the curve
  compresses, wide stays wide).
* **Air band is the star:** 2-pole matched high shelf whose *analog prototype corner* is the
  `Reach` knob, 8→40 kHz. Above ~16 kHz the corner is beyond fs/2 at 44.1 k — only a matched
  design keeps the knob meaningful there; RBJ would pin the whole shelf into a cramp and the top
  half of `Reach` would be a dead knob (the exact timidity failure mode of law 5).
* Signature = **`Silk`**: adds a second matched shelf one octave above Reach at 0.4× the Air
  gain — the "sheen on the sheen." At 0 it is off (bit-exact).
* Air gain gets a ×1.33 scale in this type only (front `Air` knob ±18 → ±24 dB effective).

#### 6 · Dynamic — the level-aware type

* Every band gets a detector: `SvfMultimode` band-pass at the band's frequency (recycle,
  `TerrainFilters.h:317`), Q = 0.8·bandQ → rectify → one-pole envelope. Attack `max(2 ms, 2/fc)`,
  release `= 8×attack`, **squared-release** on the way down (the Phase G release law — a tail that
  decelerates, no pumping edge).
* Gain ride (ratio-less, one knob): `g_applied = g_knob · σ((env_dB − T)/6)` with
  `σ` = smoothstep. Cuts fade IN as the program gets hot (de-harsh, de-boom); boosts fade OUT
  (upward-expansion sparkle on quiet passages — set a boost and it politely ducks itself on peaks).
* **🚨 BUS REALITY (law 1):** the threshold `T` is anchored to the measured program level:
  `T = −26 dBFS + Sense`, with Signature = **`Sense`** spanning **−20…+20 dB around −26 dBFS**
  (center detent = the nominal bus). Copy a hardware threshold (−10 dBFS…) and the bands never
  move — the 26 dB deficit strikes again. All preset thresholds in §8 are stated this way.
* Attack/release are NOT tempo params — the 4-bars→1/256 rule does not apply (they are
  program-relative ballistics, fixed per band; law 3 satisfied by absence).
* Probe honestly (Phase G probe-craft): a static sine measures NOTHING here — certify with the
  **AM probe** (carrier at band fc, 4 Hz 80 % depth AM) and with the three-level probe from the
  discriminator table.

#### 7 · Sculpt — no playing safe

* Gain scale ×1.67: knob ±18 → **±30 dB** per band; with `Amount` at 200 % (§5) that is **±60 dB
  of curve** — allowed, stability-clamped only (§3.5): poles stay inside r ≤ 0.9995.
* Signature = **`Ring`**: Q 2→40 log. At Q 40 the band is a resonator: +24 dB @ 1 kHz rings
  T60 ≈ 88 ms — a playable metallic ping that tracks the input (dies with the note: law 6 is
  satisfied because a biquad is passive — zero input ⇒ exponential decay, never sustain).
* Below −24 dB knob the bell blends toward a true notch (depth → −∞ as gain → min): the last
  quarter of the downward travel morphs `A → 0`, giving surgical kill *and* keeping travel live.
* This is the destructive type: whistle EQ, telephone, comb-like triple-notch patches. The failure
  point past which it is only noise: three bands at +30 dB Q 40 within an octave — ships anyway,
  the alias-free version of "trashy shit."

---

## 3. DSP core — algorithms, math, laws

### 3.1 The signal chain (per channel, fixed order)

```
in ── Focus encode (M/S or L/R select) ──►
   Tilt (1st-order Baxandall pair, pivot 700 Hz)
   ──► LOW ──► BODY ──► BITE ──► AIR ──► [Silk shelf — Air type only]
   ──► Focus decode ──► Amount is INSIDE the band gains (not a post-fader)
   ──► Mix (LINEAR crossfade with dry) ──► out
```

Serial biquad cascade — the same topology as `ParametricEQ.h:13-18`, minus the HP/LP bookends
(cut filters belong to the Filter device / synth filters; this device shapes, it does not gate;
an all-gains-flat device must NULL, and a permanently-engaged HP is the brown-knob-SSL mistake of
1979 — they fixed it in 1983, we don't re-ship it).

### 3.2 The workhorse: Simper trapezoidal SVF (recycled)

`TerrainFilters.h:302-411` already ships the Cytomic SVF ("Cytomic SVF, trapezoidal, ALL outputs —
report §2", `SvfMultimode` with LP/HP/BP/Notch/Peak/SEM). The EQ adds the three EQ tunings from
Simper's *Solving the continuous SVF equations using trapezoidal integration* (cytomic.com
technical papers — the same paper Ableton used for EQ8):

```
A  = 10^(dB/40)                       // amplitude
// BELL:        g = tan(π f/fs)        k = 1/(Q·A)   out = v0 + k·(A²−1)·v1
// LOW SHELF:   g = tan(π f/fs)/√A     k = 1/Q       out = v0 + k·(A−1)·v1 + (A²−1)·v2
// HIGH SHELF:  g = tan(π f/fs)·√A     k = 1/Q       out = A²·v0 + k·(1−A)·A·v1 + (1−A²)·v2
// core per sample (state ic1eq, ic2eq):
a1 = 1/(1 + g·(g+k));  a2 = g·a1;  a3 = g·a2
v3 = v0 − ic2eq;  v1 = a1·ic1eq + a2·v3;  v2 = ic2eq + a2·ic1eq + a3·v3
ic1eq = 2·v1 − ic1eq;  ic2eq = 2·v2 − ic2eq        // ~9 mul + 8 add per band
```

* Zero-gain transparency is exact (A = 1 ⇒ out = v0 identically) — the null test is free.
* g/k/mix coefficients tolerate per-sample ramping without instability (trapezoidal integration
  of a passive circuit) — this is why the SVF is the *default* realization for every band that is
  being actively dragged or modulated.
* The **Tilt** is two *first-order* shelves (∓t dB low, ±t dB high, both at 700 Hz) built on the
  `TPTOnePole` helper (`TerrainFilters.h:83`) — Baxandall's gentle 6 dB/oct seesaw, immune to
  cramping concerns by virtue of slope.

### 3.3 🔑 THE DECRAMPING VERDICT — mandatory, design-time-only, and where

**The problem, quantified:** the bilinear transform maps analog 0…∞ Hz onto digital 0…fs/2. Below
fs/8 (≈ 5.5 kHz at 44.1 k) warping is negligible; above it, bells squash asymmetrically and every
response is forced flat into Nyquist. A 16 kHz +10 dB bell at 44.1 k reads several dB wrong on the
high side and its upper skirt simply ceases to exist. Every "digital harshness" cliché about stock
EQs is this. Kirchhoff sells "Robust Nyquist-matched Transform", Pro-Q sells "Natural Phase" —
it is the same fix wearing marketing.

**The chosen fix — Vicanek matched biquads** (M. Vicanek, *Matched Second Order Digital Filters*,
2016, vicanek.de/articles/BiquadFits.pdf; shelves in *Matched Two-Pole Digital Shelving Filters*,
2poleShelvingFits.pdf):

1. **Poles by impulse invariance** (exact, no warping): with `ω0 = 2π f0/fs`, `q = 1/(2Q)`:
   `a2 = e^(−2·q·ω0)`;  `a1 = −2·e^(−q·ω0)·cos(√(1−q²)·ω0)` for q ≤ 1
   (overdamped branch `q > 1`: replace cos with cosh of √(q²−1)·ω0).
2. **Numerator by closed-form fit**: choose b0, b1, b2 so |H| matches the analog prototype
   **exactly at DC, at f0, and at Nyquist**, using the paper's φ-basis
   (`φ0 = 1 − sin²(ω/2)`, `φ1 = sin²(ω/2)` evaluation frames). Transcribe equations from
   BiquadFits.pdf §Matched Peaking EQ / 2poleShelvingFits.pdf for the shelf — closed form, no
   iteration, ~2× the *design* flops of RBJ and **identical runtime cost** (it is still one biquad).
3. Run it as a TDF2 biquad.

**Where each realization is used (the hybrid law):**

| Element | Realization | Why |
|---|---|---|
| Bands with fc < 5 kHz | Simper SVF | Cramping inaudible below fs/8; SVF ramps param glides most safely |
| Bands with fc ≥ 5 kHz | Matched TDF2 biquad | Audibly uncramped top octave — the entire point |
| AIR band, always | Matched 2-pole shelf | Its analog corner (Reach, 8–40 kHz) can sit ABOVE Nyquist — only a matched design keeps the knob alive up there (Maag law) |
| Tilt | 2 × first-order TPT shelf | 6 dB/oct — warping negligible |
| Dynamic detectors | SVF band-pass | Recycled `SvfMultimode`, stability under env-driven retuning |

A band crossing 5 kHz swaps realization via the standard fade-swap (§4.3) — 30 ms output
crossfade, both filters run during the fade (< 20 mul/sample extra, transient).

**The gate (write it into the harness):** at fs = 44.1 k, +10 dB Q 2 bell at 16 kHz — max |error|
vs the analog prototype magnitude across 10 Hz–20 kHz: RBJ ≥ 3 dB (fails), ours **≤ 0.5 dB**
(passes). At fs = 96 k both pass — the harness must run at 44.1 k, where users actually live.

### 3.4 Coefficient update strategy (the CPU fix for the v6 smell)

* **Design at control rate:** recompute target coefficients only when a smoothed param target
  moved, at most **once per 32 samples** per band. All trig/exp lives here. No heap: coefficients
  are 5 floats in a POD struct, written in place (never `makePeakFilter` — the v6 allocation bug,
  §0.1).
* **Glide at audio rate:** per-sample linear ramp of the 5 biquad coefficients (or g/k/m for SVF
  bands) from current→target over the param's glide time (§5 table). Linear coefficient
  interpolation between two stable biquads can transiently mis-place poles, so: clamp the per-block
  design step to ≤ 1/3 octave of fc movement; larger jumps (preset load, Type switch, dblclick
  reset) **snap the design and crossfade the OUTPUT** instead (§4.3) — never ramp through a giant
  coefficient gap. This is the comb-click law applied to biquads.
* Seed every smoother with `setCurrentAndTargetValue` before first design (the `:42-47` NaN law).

### 3.5 Stability bounds (loop-gain law statement)

**There are no feedback loops in this device** — no loop gain to budget (law 6's loop clause is
satisfied by construction; state it, don't hand-wave it). The two stability clamps that do exist:

* **Pole radius:** `a2 = e^(−ω0/Q_eff)`-family designs keep r < 1 for all finite Q; clamp
  `Q ≤ 40` (Sculpt Ring ceiling) and `fc ≤ 0.47·fs` (design headroom before Nyquist). At Amount
  200 % gains reach ±60 dB — gain lives in the *numerator* (A), poles depend on Q only ⇒ BIBO safe
  at any gain; the only ±60 dB risk is downstream headroom (§7), which is allowed.
* **Float range:** +60 dB on a −26 dBFS program = +34 dBFS ≈ 50 linear — decades below float
  limits. `isfinite` output guard carried from `ParametricEQ.h:124` anyway (NaN in = NaN
  contained).

### 3.6 Oversampling verdict: **NEVER**

An EQ is LTI — it cannot alias (nothing nonlinear, nothing modulated at audio rate). Ableton's
Hi-Quality 2× exists purely to reduce cramping; matched design achieves the same for ~0 runtime
cost instead of 2× everything. The Dynamic type's gain modulation is block-smoothed (≤ 47 Hz
envelope bandwidth) — sideband spread is inaudible, decades below any oversampling threshold.
**No Quality dropdown on this device.** (The second dropdown slot goes to `Focus` — §5.)
Linear-phase mode is likewise REJECTED: FIR latency would re-open the fb305 latency trap that
cost the distortion its hardest section (§4.4 there); minimum-phase IIR reports honest zero.

### 3.7 Focus (M/S · L/R) math

```
encode: M = (L+R)·0.5·√2 ; S = (L−R)·0.5·√2      // energy-preserving, √2 keeps M at program level
Stereo: process L and R identically (one coefficient set, two states)
Mid / Side / Left / Right: process ONLY the focused channel, pass the other bit-exact
decode: L = (M+S)/√2 ; R = (M−S)/√2
```

Focus switch = 20 ms linear crossfade between the two full matrix paths (menus never cut).
Focus ≠ Stereo costs one extra pair of adds; the un-focused channel is untouched (null-exact),
which makes `Side` + Air the cleanest widener in the synth and `Side` + Low-cut-shelf a
mono-anchor — both preset-able (§8).

### 3.8 Mix law — LINEAR, not equal-power

Dry and wet are 100 % correlated (same signal, minimum-phase filtered) — equal-power sin/cos would
bump +3 dB at 50 %. `out = dry·(1−mix) + wet·mix`, per-sample smoothed. 100 % = fully wet (law 4).
Parallel-EQ behavior at mix < 100 % is a feature (upward "Pultec parallel" tricks) — document in
the manual that partial mix *softens* curve depths (a −30 dB notch at 50 % mix is ≈ −6 dB, the
correct linear-combination math, not a bug).

### 3.9 The Amount law (the drama macro)

`Amount ∈ 0…200 %` multiplies every band's dB gain (and Tilt, and Air) *before* design:
`g_used = g_knob · typeScale · amount`. 0 % = provable flat (null test at ANY knob state — the A/B
gesture and the "what is this EQ doing" reveal); 100 % = as drawn; 200 % = the caricature.
Per-sample smoothing on `amount` itself; design updates ride the normal 32-sample cadence.
This is the knob that makes one automation lane perform the whole device.

---

## 4. The engineering musts

### 4.1 🚨 THE 4TH-DEVICE fb305 LAW — the landmine field, mapped

fb338 already generalized the exclusion sums. The three sites now read (verified today):

```cpp
// PluginProcessor.cpp:7159, :7326, :7358
const float rtdL = ((rvbSendL ? rvbSendL[i] : 0.0f) + (dlySendL ? dlySendL[i] : 0.0f)
                  + (dstSendL ? dstSendL[i] : 0.0f)) * outputGain * kVoiceToFxPad;
/* fb338 — the fb305 law: EVERY send bus joins EVERY main-send exclusion */
```

**Required, in the SAME commit that creates `eqSendBuf_`:** add `+ (eqzSendL ? eqzSendL[i] : 0.0f)`
to all three L sites and their R twins, and give the Equalizer's own main-send branch the symmetric
FOUR-way subtraction (its `rtd` must sum rvb+dly+dst). Grep `rtdL` — the count of sites grows with
each device; fix them ALL or a pill-routed osc reaches the reverb twice (the original fb305 bug,
third resurrection). `SYN_FX_ORDER` grows again too: 4 devices = 24 orderings — it is already an
insert-lambda list (fb307 grammar, memory: delay arc); the EQ registers one more insert-lambda,
no new mechanism.

### 4.2 Latency: the good news, stated once

Minimum-phase IIR, no oversampling, no FIR ⇒ **true zero latency**. No `setLatencySamples`, no
dry-side compensation delay, no downstream send-read offsets — the entire §4.4 nightmare of the
distortion bible does not exist here *because* we rejected linear phase and oversampling. Keep it
that way: any future "linear phase" feature request re-opens combing at every mix < 100 % and
breaks fb305 alignment for every downstream device. The answer is no.

### 4.3 No clicks, no crackle — the five for THIS device

1. **Per-sample glide on every continuous param** (table §5); coefficient ramps per §3.4.
2. **Type switch = fade-swap-recover:** build the new type's filter bank cold (states zeroed,
   coefficients designed), 40 ms equal-gain crossfade of OUTPUTS, retire the old bank. Both banks
   run for 40 ms (≈ +0.1 % CPU transient). Same for Focus (20 ms, §3.7) and the SVF↔matched
   realization swap (30 ms, §3.3). Character/preset jumps: snap design + output crossfade —
   never ramp coefficients across octaves (§3.4).
3. **Reset on power-on:** the device's POWER pill defaults OFF (house, `PluginProcessor.h:1549`
   grammar); on power-on, `reset()` all states then fade wet in over 10 ms — a stale biquad state
   from minutes ago is a thump.
4. **Denormals:** `ScopedNoDenormals` + flush band states when the block's input peak < 1e−7
   (biquad tails on silence are the textbook trap; Sculpt Q 40 rings into denormal range for
   ~200 ms after every note without the flush). Dynamic envelopes: same flush.
5. **Solo/audition (if the card exposes it):** recycle the smoother-burn idiom from
   `ParametricEQ.h:174-191` — every non-soloed smoother must keep advancing or release jumps.

### 4.4 Unity-through + the null gates (hard gates, run in the harness)

* Defaults (all gains 0, Tilt 0, Air 0, Amount 100 %, Mix 100 %, Focus Stereo, Type Surgical):
  **output is bit-exact input** (SVF/matched identity at A=1; Tilt TPT identity at t=0).
* Amount 0 %: bit-exact at any knob state.
* Focus Mid: R−L difference signal is bit-exact through the device.
* Power OFF: the fb303 default-sound guarantee — byte-identical bounce with the device in the
  rack, off.

### 4.5 Param plumbing (the silent-no-op laws)

New family `SYN_EQZ_*` (never reuse `EQ_B*` — §0.1): `SYN_EQZ_TYPE` choice(7) ·
`SYN_EQZ_FOCUS` choice(5: Stereo/Mid/Side/Left/Right) · `SYN_EQZ_TILT` · `SYN_EQZ_AIR` ·
`SYN_EQZ_AMOUNT` · `SYN_EQZ_MIX` · `SYN_EQZ_P1..P8` (back-8, §5) · `SYN_EQZ_SRC_A/B/C/D/SUB`
pills + POWER. Every one: the 4-point WebSliderRelay chain (relay member → `.withOptionsFrom` →
attachment → JS read) or it builds clean and does nothing (memory: verify-webview-param-bind-chain).
Choice params: **read the index directly** — `(int)*rawParam(id)`, never scale by (N−1)
(the fb50 law, CLAUDE.md §4). Choice cardinality is a preset-compat contract — pick 7 types NOW
and append only (Phase G session-law ①: choice-param cardinality is forever).

---

## 5. Chassis map — the fb275 lock, solved for an EQ

**The tension, named:** a 4-band parametric wants 12 knobs (4×F/G/Q) before Tilt/Air/dynamics —
the chassis gives 11. **The solve:** Q is never a knob — Q is a LAW owned by the Type (§2: constant /
proportional / passive-fixed / Ring), globally trimmed only by Surgical's `Width` signature. Freq+Gain
per band = 8 = exactly the back grid, heroes go front, and the *visualizer nodes edit the same 11
params* so nothing is hidden (Serum 2 gives 6 params total; Kilohearts solves it with infinite bands
in a big UI; a 500-series pedal solves it with fixed frequencies — ours is the console solve:
**fixed roles, law-driven Q**).

### 5.1 Front card — 3 heroes + Mix (+ the visualizer §6.3 + pills)

| Knob | Param | Range / taper | Glide | What it does (tooltip voice) |
|---|---|---|---|---|
| **Tilt** | `SYN_EQZ_TILT` | ±12 dB, linear-in-dB, center detent | 15 ms | One knob, whole spectrum: right = brighter, left = darker, seesaw around 700 Hz |
| **Air** | `SYN_EQZ_AIR` | ±18 dB, linear-in-dB, center detent | 15 ms | The shelf above everything — sheen up, harshness down. ×1.33 in Air type |
| **Amount** | `SYN_EQZ_AMOUNT` | 0–200 %, linear, default 100 % | 10 ms | Scales the whole curve: 0 flat, 100 as drawn, 200 double strength |
| **Mix** | `SYN_EQZ_MIX` | 0–100 %, default 100 % | 10 ms | 100 % = fully wet (law 4); below = parallel EQ |

Pills (per-type-meaningful, the reverb pill law): **`Delta`** — monitor `wet − dry` (hear exactly
what the EQ adds/removes; 3 subtractions, the cheapest wow in the device) · **`Auto`** — loudness
makeup from the curve's power integral over a pink-weighted band set, **default OFF** (distortion
§4.2 precedent). Plus the standard POWER + A/B/C/D/S routing pills (chassis).

### 5.2 Back panel — 2 dropdowns + 8 knobs (4×2, columns = bands)

Dropdowns: **`Type`** (7 — §2.1) · **`Focus`** (Stereo / Mid / Side / Left / Right).

| Col → | LOW | BODY | BITE | AIR |
|---|---|---|---|---|
| Row 1 (freq) | **Low Hz** 30–450 Hz log, glide 20 ms | **Body Hz** 120–2.5 k log, 20 ms | **Bite Hz** 800–12 k log, 20 ms | **Reach** 8–40 kHz log, 20 ms (analog corner — §2.2·5) |
| Row 2 (gain) | **Low** ±18 dB lin-dB, 10 ms | **Body** ±18 dB, 10 ms | **Bite** ±18 dB, 10 ms | **Shape** = the SIGNATURE slot, per-type relabel, 10 ms |

APVTS: P1..P8 = LowHz, Low, BodyHz, Body, BiteHz, Bite, Reach, Shape (fixed order; labels relabel
per Type — the distortion Model-A grammar, `ParameterIDs.hpp:414-421` precedent).

**The Shape (P8) relabels** (§2.2): Surgical `Width` (0.4–4× Q, log, center 1×) · British `Bump`
(S 1.0–2.0) · American `Grip` (exponent 1.0–2.5) · Passive `Dip` (0–100 %) · Air `Silk`
(0–100 %) · Dynamic `Sense` (**−20…+20 dB around the −26 dBFS program anchor**, center detent) ·
Sculpt `Ring` (Q 2–40, log). Default = center. Every relabel changes CURVE MATH, not cosmetics —
the Phase G "no pointless characters" gate applies to these seven names.

Defaults: LowHz 90 · BodyHz 450 · BiteHz 3.2 k · Reach 20 k · all gains 0 · Shape center.
No doubles anywhere (Low/Body/Bite/Air/Reach/Shape/Tilt/Amount/Mix/Type/Focus — all unique;
"Air" appears once, front). AIR band's gain deliberately lives on the FRONT (it is the hero Max
asked for); its frequency (`Reach`) lives on the back — one band, two surfaces, zero duplication.

---

## 6. Visualizers

### 6.1 How the greats draw an EQ (survey, mechanisms exact)

* **FabFilter Pro-Q 3/4 — THE grammar.** Full-window log-f display (10 Hz–30 kHz), ±3/6/12/30 dB
  vertical scales; **real-time pre- AND post-EQ analyzers** (configurable resolution/decay, 4.5
  dB/oct visual tilt so pink noise reads flat); the summed response as a bold yellow curve; each
  band a colored thin curve + a **draggable dot** (drag = freq/gain, wheel = Q, dbl-click = type);
  floating per-band text controls under the selected dot; **dynamic bands** draw a translucent
  vertical *range* fill and the dot itself slides in real time with the applied gain; **spectrum
  grab** — hover the live analyzer, grab a peak, it becomes a band; piano-roll note ruler along
  the footer; collision "EQ Match" via sidechain. Every modern EQ since is a dialect of this.
* **TDR Nova.** Nodes + wideband spectrum, plus the **threshold arc**: a curved bracket around
  each dynamic node showing threshold distance; the node fill animates with gain reduction —
  dynamics state readable at a glance without meters.
* **Ableton EQ8.** Curve + spectrum + 8 colored dots; **audition mode** (hold = band-pass solo of
  that band — our in-tree Solo, `ParametricEQ.h:97`, is exactly this); adaptive-Q toggle; 2×
  oversampled "Hi-Quality" (their anti-cramp).
* **Kirchhoff-EQ.** The maximalist: 32 bands, per-band dynamics with the sidechain range drawn
  in-curve, 30 vintage curve models in a dropdown per band — proof that "vintage personalities ×
  modern display" (our exact §2 plan) is a shipping, loved product.
* **Serum 2.** Two dots on a small static-grid curve; no analyzer. The gap we drive through.
* **The hardware counterpoint** (UAD Pultec, Maag, Arturia SITRAL faceplates): no curve at all —
  knobs on a photoreal panel; "trust the ears." Beautiful, but it violates our law 9 (everything
  audible visible) — noted and rejected.

### 6.2 What Terrain already renders (the recycle inventory, precise)

`redrawEqCanvas` (`index.html:18335`): log-f grid, −80 dB-floor spectrum polyline from
`analyzerPre/Post` (4096-FFT, 47 fps, `SpectrumAnalyzer.h`), summed IIR response curve computed
in JS from band params. `redrawEqHandles` (`:18422`): the dot layer — drag/wheel/context/dblclick
per §0.2. Dead-feed fade (`:25108`): silence ⇒ ink fades out. The fb342 card laws apply: no
per-frame `shadowBlur`/`filter`, halo pushes only when visible×fresh, rAF loop with no early
returns (session-laws ③④, delay-viz memory fb312/313).

### 6.3 The three concepts for OUR card (pick 1 + 2 as default; 3 is the Dynamic overlay)

1. **The Living Curve (primary — recycled Pro-Q grammar).** The card core = mini eq-canvas:
   dim grid, live **post**-spectrum as a filled soft-white area (dead-feed fade: idle = dim,
   playing = bright — law 9's delta for free), the summed curve as the bold stroke, **four
   tinted node dots** two-way-bound to the 8 back knobs + front Air (drag X = *Hz knob,
   drag Y = gain knob, wheel = Shape where it maps) — same binding discipline as the panel EQ
   (`getSliderState`, echo-guards per the two-way-binding law). Tilt renders as the whole
   curve's baseline pivoting — turn Tilt and the entire display leans. Type switch re-voices
   the curve math in JS (each type's Q law mirrored — the "every curve must move" filter-viz law).
2. **Delta Ink (the dramaticism layer, cheap and unique).** Both analyzers are already pre/post:
   draw the **pre** spectrum dim behind, **post** bright in front, and FILL the signed area
   between them — boost regions ignite in the accent color above the pre line, cuts carve
   dark below it. The EQ literally paints what it is doing to the live audio; at Amount 0 the
   ink vanishes (the A/B gesture made visible). Cost: one extra polygon fill per frame, both
   FFTs already push. No other synth draws this on a rack card.
3. **Dynamic Halos (Dynamic type only).** Each node grows a TDR-style arc whose radius = distance
   to threshold and whose fill pulses with the applied ride gain (block-rate push of 4 floats —
   the fb90-at-birth push law: register the push lane when the card is born, not lazily).

CPU: canvas 2D polylines at ≤ 30 fps card cadence, ≤ 4 k points/frame, zero blur — inside the
fb342 frame budget that the card-like-water pass certified.

---

## 7. Interplay — the device in the chain

* **Unity discipline:** defaults null (§4.4); a preset's curve should integrate to ≈ 0 dB
  pink-weighted where feasible, or ship with `Auto` makeup noted. The certified chain expectation:
  inserting the device powered-on-but-flat changes NOTHING downstream, byte-exact.
* **Headroom math on the real bus (law 1):** program −26 dBFS; a +18 dB Bite boost peaks at
  −8 dBFS — safe; Sculpt +30 with Amount 200 % reaches +34 dBFS *inside the float chain* —
  legal, no clamp (float headroom §3.5), but the DOWNSTREAM devices see it: Distortion's CLIP
  family at threshold −6 dBFS internal will saturate 40 dB deep (glorious, intended), and the
  master `kInstrumentMakeup` stage will hard-hand it to the DAW. Document, don't police
  (no-playing-safe; the limiter is the user's mix).
* **Ordering wisdom (the classic laws, for the manual + default drag position):**
  EQ **before** Distortion = drive shaping (a +6 dB Bite into Tube changes WHAT saturates —
  the pre-emphasis trick from distortion §4.3, now user-patchable);
  EQ **after** Reverb/Delay = tone-shaping the tail (Air on a hall = instant "expensive");
  Dynamic EQ **before** Reverb tames the boom that would otherwise bloom in the tail.
  Default insert position: LAST in the drag order (polish role), user-reorderable (fb307).
* **Stacking:** two EQ devices in series is mathematically just one bigger EQ (LTI commutes) —
  no interaction traps; CPU is the only cost. EQ inside a feedback path does not exist here
  (no sends into device feedback anywhere in the rack grammar).
* **What breaks when stacked:** Sculpt ring (Q 40) feeding Delay's feedback ≥ 70 % turns the ring
  into a sustained pitched drone — the DELAY's loop-gain law already caps its loop below unity
  (fb306-310), so it decays; do not "fix" the EQ, the delay owns its loop. Focus=Side boosts fed
  into a mono-summing downstream (Splitter M/S future device) cancel — mono-compat is the user's
  ear, but the manual gets a note.
* **Mod matrix:** band freqs/gains as mod destinations are DEFERRED (the panel EQ already offers
  modulated bells at `PluginProcessor.cpp:6853`; the device ships without mod lanes v1 — §12 Q4).

---

## 8. Factory presets (14 sketches — name · intent · rough values)

All values: Type · Focus · front (Tilt/Air/Amount) · bands (Hz/dB) · Shape. Unstated = default.

1. **Init Flat** — Surgical · Stereo · all zero. The canvas.
2. **Air Lift** — Air · Stereo · Air +8 · Reach 28 k · Silk 40 %. The one-knob "expensive" button.
3. **Pultec Bass Trick** — Passive · Stereo · Low 60 Hz +6 · Dip 65 % · Air −2. Kick/bass focus + auto-scoop at ~250 Hz.
4. **Smile** — American · Stereo · Low 90 +5 · Body 450 −3 · Bite 3.2 k +4 · Air +3. The hi-fi V.
5. **De-Mud** — American · Stereo · Body 300 −6 (Grip 1.8 → tight only when deep) · Tilt +1.
6. **Presence Push** — British · Stereo · Bite 4 k +6 · Bump 60 % · Low 100 +2. The 1073 vocal move.
7. **Telephone** — Sculpt · Stereo · Amount 140 % · Low −18 @150 · Air −18 · Body +12 @1.2 k · Ring Q 8. Instant lo-fi; automate Amount 0→140 for the drop reveal.
8. **Side Sparkle** — Air · **Side** · Air +10 · Reach 32 k. Width without a widener.
9. **Mono Anchor** — Surgical · **Side** · Low 120 −18 · Width 0.7×. Tightens stereo lows to mono.
10. **Dynamic De-Harsh** — Dynamic · Stereo · Bite 6 k −8 · Sense +6 dB (fires above program) · Air +4. Cuts only when it hurts, air stays.
11. **Quiet Bloom** — Dynamic · Stereo · Body 250 +6 · Sense −8 dB (below program ⇒ boost fades OUT on peaks) — swells warm on decays.
12. **Tape Tilt** — Surgical · Stereo · Tilt −5 · Air +3 @ Reach 24 k. Darker but still open — the mix-glue tilt.
13. **Whistle Ring** — Sculpt · Stereo · Bite 2.8 k +24 · Ring Q 40 · Amount 100. The dangerous one; T60 ≈ 30 ms metallic ping on every transient.
14. **Broadcast** — British · Stereo · Low 100 +4 · Bite 4 k +6 · Air +4 · Bump 40 % · Auto pill ON. The Sie-Q "always good" voicing.

Preset level spread gate (Phase G lesson): certify all 14 within ±3 dB pink-weighted of Init at
Amount 100 (Whistle Ring exempted, flagged loud by design).

---

## 9. CPU budget

Per stereo instance @ 48 k, Apple-silicon class (numbers are estimates to be certified by
`eqz_cpu` harness, `clang++ -O2 -I shim -I Source` like `dst_cpu`):

| Element | Cost | Notes |
|---|---|---|
| 4 bands × biquad/SVF × 2 ch | ~72 mul+add /sample | The whole engine core |
| Tilt (2 × 1-pole × 2 ch) | ~16 /sample | |
| Focus encode/decode | 8 /sample | Only when Focus ≠ Stereo |
| Coefficient design | ~300 flops / 32 samples / moving band | Zero when knobs still |
| Dynamic type add-on | +4 SVF-BP + 4 env / ch | ≈ +45 % of core, still trivial |
| Spectrum push | already paid | `analyzerPre/Post` exist; gate by card-visible (`vizLive` grammar `PluginProcessor.cpp:6865`) |
| **Total** | **≈ 0.1–0.25 % of one core** | vs distortion's post-fb342 ~1.5 %; Serum-bar compliant |

Tiering: none needed (no Quality dropdown — §3.6). Control-head sleep (session-law ⑥): when
POWER off, zero work; when on but card hidden, no viz pushes. Coefficient design skips entirely
when all smoothers are settled (dirty-flag per band — the fix for the v6 always-recompute bug).

---

## 10. Pitfalls — the collected traps

1. **The v6 allocation bug** — never call `make*Filter` factories on the audio thread (§0.1).
2. **Q=0 NaN poison at prepare** (§0.1 / `ParametricEQ.h:42-47`) — seed smoothers first.
3. **Coefficient-ramp instability** — ramp only ≤ 1/3-oct design steps; snap+output-crossfade
   beyond (§3.4). Zipper on freq knobs is the audible symptom of skipping this.
4. **Cramping** — RBJ above 5 kHz at 44.1 k is a measurably wrong EQ; Reach would be a half-dead
   knob (law-5 violation) without matched shelves (§3.3).
5. **fb305 exclusion sums** — the 4th send bus must join ALL `rtd` sites in the same commit (§4.1).
6. **Denormal tails** — Sculpt Q 40 + silence; flush + `ScopedNoDenormals` (§4.3-4).
7. **Mono-sum collapse** — Side-focus boosts vanish (or comb) on mono playback; manual note, and
   the Delta pill makes it audible instantly.
8. **Equal-power mix bump** — correlated wet/dry needs LINEAR mix (§3.8); copying the reverb's
   sin/cos law here adds a +3 dB center bump.
9. **Dynamic threshold copied from literature** — lands 26 dB wrong (law 1); anchor to −26 dBFS
   (§2.2·6). Probe with AM, not static sines (Phase G probe law).
10. **DC**: shelves at max low boost amplify any upstream DC by 8×; the FX bus is AC-clean today
    (distortion's blockers, §4.1 there) — add NO blocker here (it would break the null test);
    assert DC-free in the harness instead.
11. **Preset-compat cardinality** — 7 Types, 5 Focus choices locked at birth; append-only forever.
12. **Two EQ surfaces confusion** — the synth-panel EQ and the device must never share param IDs,
    presets, or JS ids (`eqB1*` vs `eqz*`) or state restoration cross-wires (state-persists law).
13. **UI line-number drift** — every `index.html:` number in this file drifts per build; re-grep.

---

## 11. Hard-rule compliance checklist (laws 1–10, walked)

1. **Bus reality (−26 dBFS):** Dynamic `Sense` anchored to −26 dBFS ±20 (§2.2·6, §5.2); headroom
   math stated for boosts (§7); no literature thresholds anywhere. ✅
2. **Chassis fb275:** front 3 + Mix (Tilt/Air/Amount), back 2 dropdowns (Type/Focus) + 8 knobs
   4×2 = 11 params, pragmatic Title-case names, signature relabel grammar (§5). ✅
3. **Time params 4 bars→1/256:** no tempo-relevant times exist; Dynamic ballistics are
   program-relative ms, exempt by kind (§2.2·6). ✅ (stated, not skipped)
4. **Mix 100 % = fully wet; switches never cut:** §3.8; Type 40 ms / Focus 20 ms / realization
   30 ms crossfades (§4.3). ✅
5. **Params evolve 0→100, max = just past useful:** every knob's law stated with live travel
   (proportional laws make even small moves read); Amount to 200 %, Sculpt ±30 dB Q 40 destructive
   maximum; decramping keeps Reach's top half alive; 7 Types each carry a measured discriminator
   (§2.1 table) — near-twins pre-cut. ✅
6. **Nothing free-runs + loop gain:** no feedback loops (stated §3.5); resonant rings are passive
   decays that die with input; Dynamic envelopes idle at silence. ✅
7. **No clicks:** §4.3 five-point plan; coefficient ramp law §3.4. ✅
8. **CPU:** ≈ 0.1–0.25 % core, no oversampling ever, dirty-flag design, control-head sleep (§9,
   §3.6). ✅
9. **Audible ⇔ visible + dramatic:** Living Curve + Delta Ink, dead-feed fade idle-dim, every
   param mirrored in curve math incl. Type Q-laws and Tilt lean (§6.3). ✅
10. **Recycle first:** SvfMultimode, TPTOnePole, SpectrumAnalyzer + both instances + push sites,
    eq-canvas/eq-handles UI wholesale, Solo idiom, smoothing idioms, pmenu presets, engine-select
    dropdown, DistortionEngine device-class grammar + POWER-off default — all verified by reading,
    file:line in §0/§13. ✅

---

## 12. Open questions for Max

1. **Band count:** 4 fixed-role bands is the chassis-honest solve. If you want 6+, the only paths
   are band-select editing (breaks "no hidden state" feel) or nodes-only extra bands (breaks
   knob=param parity). Confirm 4.
2. **Front trio:** Tilt · Air · Amount is the proposal (Air the hero you asked for). Alternative:
   swap Amount for an output Trim ±12 dB. Amount is the more dramatic automation lane — confirm.
3. **Dynamic type in v1?** It is the most expensive spec item (detectors, probe harness, Sense
   calibration). Ship v1 with all 7, or land 6 and add Dynamic in the first update? (Cardinality
   law means the choice list must include it at birth either way — it can ship disabled.)
4. **Mod destinations** for band freq/gain (the panel EQ already has them): v1 or defer? Defer
   recommended — the device's Amount + Dynamic cover most musical motion.
5. **Delta pill vs a second pill candidate** (`Solo` audition on node-hold, Ableton-style, using
   the in-tree solo grammar): both are cheap; chassis pill budget is the constraint. Pick two of
   Delta / Auto / Solo.
6. **Sculpt ceiling:** ±30 dB per band (±60 with Amount 200 %) — bless, or push further?
7. **Does the device eventually REPLACE the synth-panel EQ** (one EQ everywhere, the device
   engine backported), or do both live forever? Affects how much polish the v6 engine gets.
8. **Name check — RESOLVED by law, no decision needed:** the card is `Equalizer`. `EQ` is
   impossible: the synth-panel switcher button is already literally labeled `EQ`
   (`index.html:5178`, verified) — naming the rack card `EQ` would be the same name twice
   (no-doubles law), and `Equalizer` also matches the full-word pattern of the existing cards
   (Reverb / Delay / Distortion) plus the Title-case mandate. Nothing left to confirm.

---

## 13. Recycle inventory (verified by reading, this session)

| Asset | Where | Use |
|---|---|---|
| Cytomic trapezoidal SVF, all outputs | `TerrainFilters.h:302-411` (`SvfMultimode`, outputs enum `:319`) | Band core < 5 kHz + Dynamic detectors; add the three EQ tunings (§3.2) |
| TPT one-pole | `TerrainFilters.h:83` | Tilt's two first-order shelves |
| Spectrum feed | `SpectrumAnalyzer.h` (4096 FFT, 47 fps, lock-free) · instances `PluginProcessor.h:924` · pushes `PluginProcessor.cpp:6865-6869` | Card spectrum + Delta Ink; re-tap around the device, `vizLive`-gated |
| Pro-Q-grammar UI | `index.html` `#eq-canvas`/`#eq-handles` `:5534-5535`, `redrawEqCanvas :18335`, handles `:18422-18834` | The card visualizer, namespaced `eqz*` |
| Solo audition | `ParametricEQ.h:94-97, 174-191` | Optional Solo pill; smoother-burn idiom mandatory |
| NaN-seed law | `ParametricEQ.h:42-47` | Carried verbatim into the new engine |
| Device-engine class shape + POWER-off default | `DistortionEngine.h:59`, `PluginProcessor.h:1549` | `EqualizerEngine` mirrors the grammar |
| Exclusion-sum law + sites | `PluginProcessor.cpp:7159/:7326/:7358` (fb338 comment) | §4.1 — the 4th bus joins every sum |
| Smoothing idioms | `DelayEngine.h` `xC += (xT−xC)*smth`; 5 ms LSV pattern `ParametricEQ.h:50-52` | All param glides |
| Preset menu / dropdowns / pills | `TIC.presets` `.pmenu` glass; `engine-select` idiom; fx-rack chassis `Design/fx-rack-v7-CANONICAL.html` | Card + back panel UI, zero new widgets |
| Insert-lambda FX ordering | fb307 `SYN_FX_ORDER` grammar | One more lambda for the EQ |

## 14. Sources

**Serum 2 / compact EQs:** musicradar.com/how-to/a-quick-guide-to-xfer-records-serums-effects ·
edmprod.com/serum-2-guide/ · sonic-weaponry.com/blogs/free-production-tutorials-and-resources/serum-2-released ·
kilohearts.com/products/slice_eq · kilohearts.com/docs/slice_eq
**Decramping / filter math:** vicanek.de/articles/BiquadFits.pdf · vicanek.de/articles/2poleShelvingFits.pdf ·
vicanek.de/articles/ShelvingFits.pdf · aes.org/e-lib/browse.cfm?elib=7854 (Orfanidis 1997) ·
aes.org/e-lib/browse.cfm?elib=16077 (Massberg 2011) · cytomic.com/technical-papers/ (Simper SVF) ·
vladgsound.wordpress.com/2015/01/12/a-classification-of-digital-equalizers-draft/ ·
production-expert.com/production-expert-1/what-is-eq-cramping-and-should-you-care ·
science-of-sound.net/2016/07/oversampling-digital-equalizers/ ·
intelligentsoundengineering.wordpress.com/2018/04/28/analogue-matched-digital-eq-how-far-can-you-go-linearly/
**Lineage / hardware:** pulsetechniques.com/products/tube-equalizers/eqp-1a/ ·
uaudio.com/blogs/ua/pultec-collection-tips-and-tricks · abbeyroadinstitute.nl/blog/demystifying-the-pultec/ ·
penny.cool/go-ahead-boost-and-attenuate-the-pultec-eqp-1a/ · en.wikipedia.org/wiki/Pultec_EQP-1 ·
uaudio.com/products/api-500-series-eq-collection (proportional Q) ·
help.uaudio.com/hc/en-us/articles/4419497223188-Neve-1073-Preamp-EQ-Manual ·
maagaudio.com/manuals/Maag%20Audio%20EQ4%20User%20Guide.pdf · soundonsound.com/reviews/maag-audio-eq4 ·
mouseplugins.com/en/blog/maag-eq4-air-band-explained · soundtoys.com/product/sie-q/ ·
arturia.com/products/software-effects/eq-sitral-295/overview ·
softube.com/us/plug-ins/tonelux-tilt · waves.com/ssl-e-channel-or-g-channel ·
abbeyroadinstitute.nl/blog/ssl-e-g-series-eq/ ·
vintagedigital.com.au/milestone/parametric-equaliser/ · massenburg.com/wp-content/uploads/2019/03/GM_ParaEQ.pdf
(GM's own history — confirms the 1972 AES preprint + the single-op-amp T-filter topology) ·
Massenburg, *Parametric Equalization*, AES 42nd Convention preprint, 1972
**Modern references:** fabfilter.com/help/ffproq3-manual.pdf · fabfilter.com/help/pro-q/using/analyzer ·
threebodytech.com/en/products/kirchhoffeq · tokyodawn.net/tdr-nova/ · docs.tokyodawn.net/nova-manual/

*End of bible. A builder should be able to implement `EqualizerEngine.h`, the `SYN_EQZ_*` param
family, the card, and the harness from this file plus the cited repo lines — without re-research.*
