# DELAY FX Device — Consolidated Research

**Project:** Terrain Instrument (JUCE 8 WebView synth, Waves Crate)
**Purpose:** Design a chainable, per-routable DELAY device for the frozen **v7 FX rack** chassis.
**Chassis budget:** 11 params/device = **3 params + Mix on the front**, **8 knobs (4×2) + up to 2 dropdowns on the back**.
**Bars held:** Serum 2 is the reference; learn from the greats; DRAMATICISM (every param night-and-day, every type distinct); NO clicks/crackle; CPU-friendly (least-CPU-that-sounds-great).
**Provenance:** Synthesized from 11 parallel research agents (Serum, Valhalla, Arturia, Soundtoys EchoBoy, u-he + Kilohearts, fractional-delay DSP ×2, stereo routing, timing/feedback, mockup/Web-Audio). Claims are labeled **[FACT]** (documented in a cited source) or **[INFER]** (DSP-grounded inference). Every source URL is carried in the inline citations and the reference table (§7).

---

## 1. Executive Summary + Recommended "Essential" Delay Types

A delay's identity comes from **two orthogonal axes** that every serious reference plugin keeps separate:

1. **CHARACTER** — the coloration of a *single repeat* (Digital / Tape / BBD-analog / Diffuse-Ghost). This lives *inside* the delay line's feedback loop.
2. **ROUTING** — how the two delay lines are wired (Normal / Ping-Pong / Dual / Mono). This is a feedback-matrix + input-sum choice, **orthogonal to character**.

Valhalla makes this explicit ("Mode = algorithm/character" vs "Style = routing"), and so do Serum, EchoBoy, and Arturia. **The single most important framing decision for us: character is the TYPE dropdown; routing is a control (dropdown #2 or a knob). Do NOT burn a "type" slot on ping-pong.** (valhalladsp.com/2019/04/16/valhalladelay-the-mode-control, valhalladsp.com/2019/04/16/valhalladelay-the-style-control) **[FACT]**

### Recommended essential TYPES (character engines) — and why these

We recommend **4 character types**, chosen because each owns a *different perceptual axis* so no two feel the same (the dramaticism test), and all four share ~95% of one engine (the CPU test):

| Type | Owns the axis of… | Why essential |
|---|---|---|
| **Digital (Clean)** | *nothing* — the phase-clean, full-bandwidth control | The A/B anchor. Precise dotted-8th/ping-pong workhorse. Cheapest path. |
| **Tape** | *pitch/time motion* — wow/flutter + repitch pitch-bend on time change, per-repeat darkening + saturation | The universally-loved "analog echo" (Space Echo/Echoplex). Time changes bend pitch — instantly not-digital. |
| **BBD (Analog)** | *dark + companding noise-pump* — aggressive band-limit + breathing noise | The darkest, grittiest, most lo-fi voice. Owns "underwater" that Tape can't. |
| **Diffuse / Ghost** | *transient destruction + inharmonic drift* — allpass smear (delay→reverb morph) + optional frequency shift | The sound-design standout; bridges to the reverb device; the "unreal, clangorous" tail. |

**Why NOT more types.** Pitch-shimmer (harmonic transpose) and Reverse are compelling (Valhalla Pitch/RevPitch) but each needs a real granular/PSOLA shifter (CPU + artifact risk) and would crowd the 8-knob back panel. Recommendation: **ship the 4 above for v1**; fold a "Shift" knob into the Ghost/Diffuse type (frequency-shift = inharmonic) and keep harmonic Pitch-shimmer as a **documented v2 stretch**. Spring/Twang (u-he Twangström) is genuinely distinct but is really a reverb-adjacent physical model — better suited to a future reverb-family device than the delay. (valhalladsp.com/shop/delay/valhalladelay, u-he.com/products/twangstrom) **[INFER]**

**Time-change behavior is the identity lever.** Make Digital = crossfade (no pitch shift), Tape/BBD = repitch glide (pitch bends), Diffuse = crossfade + smear. This maps to the industry's three documented time-update models (Bitwig/UA "Repitch vs Fade", FabFilter Timeless "Tape vs Time-stretch"). (bitwig.com/userguide, fabfilter.com/help/timeless) **[FACT]**

---

## 2. Taxonomy — Character Engines vs Routing Modes

### 2a. CHARACTER ENGINES (the "type")
The character block wraps the shared delay line. Different **degradation mechanisms** (not just EQ) are what make types dramatic — Valhalla's core lesson (valhalladsp.com/2022/10/21/valhalla-modes-algorithms) **[FACT]**:

- **Digital** — clean, full bandwidth. Degradation is *quantization/bit-crush* (Valhalla "Age" = bit-depth; Arturia Eternity bit-crusher + downsample "ghost partials"). Feedback stays bright. **[FACT]**
- **Tape** — soft saturation (tanh, even-harmonic bias), HF roll-off per repeat, **wow (slow ~0.5–2 Hz) + flutter (fast ~6–12 Hz)** pitch drift, tape-splice/asperity noise. Self-oscillates warmly. (RE-201/Echoplex; Arturia TAPE-201; EchoBoy Studio/Space) **[FACT]**
- **BBD (analog)** — *severe band-limit* (dark, LP ~2.5–5 kHz), **companding noise-pump** (compress-in/expand-out around the line → level-dependent "breathing" hiss), clock/quantization grit, sine chorus/vibrato. Max time short (Arturia BBD Size: 40–400 ms or 100–1000 ms). Feedback muddies, not brightens. (u-he Colour Copy; Arturia Memory-Brigade; DAFx Raffel & Smith BBD model) **[FACT]**
- **Diffuse / Ghost** — HiFi-tape repeats + **allpass diffusion network** (smears attacks; at max morphs to reverb) + optional **single-sideband frequency shift** in the feedback path (Bode shifter, Hilbert transform) → inharmonic, ever-drifting, metallic. (Valhalla Ghost / FreqEcho; Serum 2 Echobode) **[FACT]**

### 2b. ROUTING MODES (orthogonal to character)
All routing is one 2×2 feedback matrix on the same pair of delay lines. Inputs:
```
dL_in = inL_routed + a*fbL + b*fbR
dR_in = inR_routed + c*fbR + d*fbL
```
where `a,c` = self-feedback, `b,d` = cross-feedback. (Basis: Gerzon/JOS unitary FDN cross-coupling; dsprelated FDN_Reverberation) **[FACT/INFER]**

- **Normal (stereo)** — `a=c=fb, b=d=0`, no input sum. L stays L, R stays R. Most mono-safe, preserves a stereo source's image.
- **Ping-Pong** — `a=c=0, b=d=fb` (pure cross-feedback), input **summed to mono** and injected into one side first → energy alternates L,R,L,R. Confirmed by Valhalla's own description and KVR ("swap the channels at feedback"). Inherently fairly **mono-compatible** (alternating amplitude-panned taps don't comb on fold-down). (kvraudio.com/forum viewtopic t=330754) **[FACT]**
- **Dual** — `a=fbL, c=fbR, b=d=0` with **independent L/R times AND independent feedback**. Complex asymmetric patterns. (Valhalla Dual/Ratio) **[FACT]**
- **Mono** — sum input, single line, wet copied to both. The anti-width anchor (bass-safe echoes).
- **Cross-morph knob (the Valhalla "fbMix" trick)** — continuously interpolate the matrix: `a=c=fb*(1−x), b=d=fb*x`. x=0 → Normal, 0.5 → 50/50 bleed, 1 → pure Ping-Pong. **Musically superior to a hard switch.** Strongly consider exposing as a "Stereo/Cross" back knob. (valhalladsp.com/2019/04/16/valhalladelay-the-style-control) **[FACT]**

### 2c. How WE frame types vs modes (recommendation)
- **Dropdown #1 = TYPE** (character): Digital / Tape / BBD / Diffuse. Switching it **re-voices** the device (mode-specific defaults) *and* re-labels 2 back knobs (Valhalla's "mode-specific Age/Mod" law). **[FACT/INFER]**
- **Dropdown #2 = ROUTING + SYNC**: Normal / Ping-Pong / Dual / Mono, plus the sync grid (Free ms / 1/4 / 1/8 / 1/8T / 1/8D / 1/16). The per-routable A/B/C/D/S/N pills already handle bus routing; this dropdown is the *internal stereo topology*.
- Ping-pong stays a **routing**, never a type. This preserves all four character types × four routings = 16 distinct combinations from one engine.

---

## 3. Per-Type Spec — Params, Ranges, Least-CPU DSP, Distinctness

**Shared core (all types).** One stereo circular buffer (~2.5 s), power-of-two sized (mask-wrap, cheaper than modulo), allocated once. Read at a fractional index with **4-point cubic/Hermite interpolation** as the default (see §4 for why not allpass). Feedback loop order: `read → in-loop tone filter (LP+HP) → character block → soft-clip (tanh) → feedback gain → write`. Filters/saturation live *inside* the loop so they compound per repeat (the signature darkening). Glide delay-time targets (~2.5–30 ms one-pole) so pointer never jumps. Flush denormals (`+1e-20` in the feedback accumulator or FTZ). Clamp feedback ≤ ~0.98 in UI; soft-clip bounds runaway. (JOS/CCRMA; project COMB-CLICK law) **[FACT]**

### Digital (Clean)
- **Defining params:** Time (1 ms–2 s / synced), Feedback (0–0.98), Tone (bipolar LP↔HP), Age→bit-crush (16→~6 bit).
- **DSP:** cubic read; **crossfade** two taps on time change (~20–50 ms equal-power) so pitch never shifts; no saturation unless Age>0 (`round(y*L)/L`). Cleanest path — no oversampling needed.
- **Distinct because:** the only type with zero coloration + zero pitch artifact. Repeats are spectral clones. The control group.

### Tape
- **Defining params:** Mod Rate (wow 0.5–2 Hz + flutter 6–12 Hz layered), Mod Depth (0–±8 ms), Age (asperity noise + splice artifacts + saturation), Tone (LP ~4–8 kHz).
- **DSP:** **repitch** time-change (single pointer glides → Doppler pitch-bend, never crossfaded); modulate read index with summed wow+flutter LFOs; tanh soft-clip (asymmetric for even harmonics); optional low-mid "head bump" ~150 Hz; filtered pink noise × Age. Multi-tap head option (RE-201 heads at t, ~1.9t, ~2.9t) for the Space-Echo rhythmic voice. Oversample **only** the saturation sub-block (2×). (Arturia TAPE-201 Motor Inertia = the repitch glide time-constant.) **[FACT]**
- **Distinct because:** pitch physically wobbles + bends on time change; repeats warm/darken. Instantly not-digital.

### BBD (Analog)
- **Defining params:** Tone (fixed steep LP ~2.5–5 kHz — the dark signature), Age (BBD hiss + compander pump), Mod Rate/Depth (sine chorus 0.1–6 Hz), Drive (Input-Level grit), BBD Size (2-pos: short 400 ms / long 1000 ms).
- **DSP:** anti-alias LPF → **compressor (fast, ~2:1)** → delay line → **reconstruction LPF whose cutoff tracks delay time** (longer = darker) → **expander** with a fixed additive noise floor that the expander *pumps* with signal level (the breathing). Gritty saturation. Model internal rate ∝ 1/time for authentic darkening. Watch **compander overshoot** on fast tap-mod (u-he's explicit warning) — smooth attack/release + limiter guard. (u-he Colour Copy signal flow; DAFx BBD paper; MN3005 = 4096 stages) **[FACT]**
- **Distinct because:** steep low LP + level-pumped noise + murky self-oscillation. "Underwater lo-fi."

### Diffuse / Ghost
- **Defining params:** Diffusion (0–100%, 4–8 allpass smear), Shift (±500 Hz frequency shift in loop), Feedback (0–0.9), Tone.
- **DSP:** cascade **4–8 Schroeder/Dattorro allpasses** (`y = −g·x + x[n−d] + g·y[n−d]`, g 0.5–0.7) with **mutually-non-multiple spread delay times** (e.g. 2/110/134/149/164/175/189/200 ms) to avoid metallic ring. Diffusion knob crossfades dry→diffused + scales stage count/g. Frequency shift = SSB via **Hilbert transform** (8–12 allpass phase-difference sections → analytic I/Q → multiply by quadrature oscillator at Shift Hz → take real part); L/R shift offset for stereo shimmer. Because energy moves each pass, feedback can exceed 1.0 without pure resonant runaway (Costello's "complexity in time-varying systems"). (valhalladsp.com/2011/01/21/reverbs-diffusion-allpass; valhalladsp.com/2010/05/13/feedback-anti-feedback; signalsmith-audio.co.uk lets-write-a-reverb) **[FACT]**
- **Distinct because:** turns discrete echoes into a wash (delay→reverb bridge) and the only type whose pitch relationship *breaks* (inharmonic Hz drift).

---

## 4. THE SERUM 2 "HIGH QUALITY" ANSWER (headline)

### What Serum 2's HQ actually is
**[FACT]** Serum 2's official "What's New" PDF lists, under *New and Enhanced FX*, the Delay as **"New HQ mode (now default)"** — that is the *only* first-party statement. It is new in v2, on by default, and Xfer publishes **no** description of what it changes internally. Serum 1's delay had no such switch, and Serum 1's only "Oversampling" control is oscillator-Warp-only, explicitly *not* an FX control. (static.xferrecords.com/Serum 2 What's New.pdf; s3.amazonaws.com/decembercymatics/Serum_Manual.pdf)

Serum 1's delay (Serum 2 keeps the same control set) = Feed(back), BPM-sync toggle, Link, per-channel base **Time + a scalar Offset** (sticky **TRIP=1.333 / DOT=1.5** — this is how dotted/triplet is reached, *not* a separate button), a single wet **Freq + inverted-Q bandpass** filter (high Q = *widest* passband = least filtering), Mix, and **3 modes: Normal / Ping-Pong / Tap→Delay**. (Serum manual p.26) **[FACT]**

### The precise cause of the phasing Max hears
**[INFER — DSP-grounded, high confidence]** HQ upgrades the **fractional-delay interpolation** used to read the buffer. Non-HQ ≈ cheap **linear** (2-point) interpolation; HQ ≈ a **flat-magnitude / nonlinear-phase** interpolator (first-order **allpass/Thiran** class, possibly plus oversampling of the feedback/saturation/filter path).

The DSP facts that pin this down (JOS/CCRMA, dsprelated PASP) **[FACT]**:
- **Linear** interpolation has **gain (magnitude) distortion** — it rolls off highs (worst when the fractional part = 0.5), and the roll-off depends on the fractional delay. It is otherwise **phase-benign**. → non-HQ sounds *duller/darker but not phasey*.
- **Allpass/Thiran** interpolation (same cost: 1 mul, 2 adds) has **flat magnitude at all frequencies** ("no gain distortion" = crisp highs = "high quality") but introduces **nonlinear phase near Nyquist**: components near fs/2 are "delayed by different amounts than other frequencies" and "slide out of alignment." **That frequency-dependent phase, not amplitude, is the audible phasing.**

Two structural facts of Serum's delay **amplify** it into what Max hears:
1. It runs in a **feedback loop**, so the near-Nyquist phase warp **accumulates a little more on every repeat** → the top end of successive echoes progressively slides out of phase = a slowly-deepening phasey/metallic shimmer on the tail.
2. **L and R have independent times** → different fractional parts → different allpass phase responses → the two channels **decorrelate at the top end** → perceived width + a comb/phaser coloration between channels.

Both are inherent to a flat-magnitude/nonlinear-phase read; both vanish with linear interpolation (which instead just dulls the highs). **This is the mechanism, not folklore.**

### Feature vs artifact
**[INFER]** It is a **side effect** of choosing the crisper flat-magnitude read, **not** a deliberately-dialed phaser. But Xfer judged the extra top-end clarity worth the trace phase color and **shipped it ON by default** — so treat it as pleasant *character*, not a bug.

**Honesty flag [FACT]:** we could **not** find any quotable user report (KVR official Serum-2 thread across multiple pages, Gearspace, Reddit) explicitly saying "HQ delay sounds phasey." The "phasey-when-HQ" claim is **unverified in the wild** — but the DSP mechanism above fully explains the symptom Max reports, and the comb-filter-on-mono-sum mechanism (Sweetwater) is an additional, always-present contributor in Normal mode with short near-equal L/R times.

### Our decision
**Reproduce genuine highest quality AND separate the phasey character into its own selectable flavor** (so the two are dramatic opposites rather than one muddying the other):

1. **Default "true HQ" = transparent.** Read with an **odd-order Lagrange (4–6) or short windowed-sinc**, applied **identically on L and R**. This is near-flat magnitude **AND near-linear phase** → bright highs with **no comb color, no dispersion, no phasey tail**. This proves the crucial point: **HQ need not be phasey — that reputation belongs specifically to allpass, not to sinc.** (JOS windowed-sinc: ~13 zero-crossings ≈ 27-tap FIR reaches transparent linear-phase fractional delay.) **[FACT]**
2. **Keep Serum's character as a deliberate option** — either the **Tape/Analog type** (which already has motion) carries a modulated-allpass "smear," or expose a small **"Character/Smear"** amount that adds first-order allpass interpolation + L/R fractional offset. Manage allpass hazards: restrict fractional delay to ~0.1–1.1 samples, ramp the coefficient (~2.5–10 ms), glide integer length → no clicks. **[FACT]**
3. **Our "HQ" toggle** (matching Ableton's documented behavior — "enables a higher quality interpolation for the delay lines when using Repitch or Fade smoothing mode"): **off = cubic Lagrange; on = windowed-sinc and/or 2× loop oversampling.** Oversampling is the highest-leverage move (moves cubic worst-case error + images above the audio band AND de-aliases feedback saturation for free) and is often cheaper than a 27-tap sinc at 1×. **Report PDC/latency correctly and delay the dry path to match** — Colour Copy's HQ changes latency, and mismatched dry/wet will phase-cancel (a known trap). (ableton.com/manual; uhe ColourCopy guide) **[FACT]**

---

## 5. Stereo Routing / Ping-Pong / Cross-Feedback + Width

- **Unified matrix** (§2b) is the whole thing. Every mode is a matrix + input-sum preset. Implement once.
- **Which side pings first** = which side the summed input hits first; expose via **Width sign** (Valhalla WIDTH −100% = right-first). **[FACT]**
- **Width = mid-side on the WET output only, never in the loop** (or it compounds per repeat and destabilizes): `M=(yL+yR)/2, S=(yL−yR)/2; wetL=M+w·S, wetR=M−w·S`. w=0 mono-collapse, w=1 normal, w>1 super-wide, w<0 swaps sides. **[FACT]**
- **L/R time offset** has two uses: (1) **musical ratios** — L=1/8, R=1/8-dotted → the classic dub bounce; (2) **Haas widening** — a sub-35 ms (sweet spot 10–30 ms) offset with zero feedback reads as width via the precedence effect. A **Link/Offset** control (R = L + Δ) covers both. **[FACT]**
- **In ping-pong, per-channel feedback is meaningless** (energy always crosses) — grey out / repurpose the second feedback; keep both time knobs live (asymmetric ping-pong = swung rhythms). **[FACT]**
- **Mono-compatibility (a Waves Crate hard rule).** Safest→riskiest: Ping-Pong (amplitude-panned alternating taps, sums cleanly) → Dual with slightly different L/R times → amplitude width → **DANGER: polarity/out-of-phase width >100% (cancels in mono)**. **Build width from panning + time-offset, not polarity.** Offer a mono-fold check button in the mockup + a perceptual mono-fold metric. (gearspace; fractalaudio forum; EchoBoy super-stereo warning) **[FACT]**

---

## 6. Timing — Sync/Tap/Free, Time-Change, Feedback Path, Diffusion, Ducking, Freeze

- **Sync math [FACT]:** `delay_ms = (60000/BPM) × noteMult`. Quarter@120 = 500 ms. Multipliers vs quarter=1.0: whole 4, half 2, 8th 0.5, 16th 0.25; **DOTTED ×1.5** (dotted-8th=0.75 = the most popular modern setting), **TRIPLET ×2/3**. Note set MUST include straight/dotted/triplet. Sync-offset knob (50–200% of synced value) is a pro touch (FabFilter). Tap = measure interval → free ms (quantize to note if sync on).
- **Three time-change behaviors [FACT]** (the #1 character differentiator; expose as part of the TYPE identity, not a separate knob):
  - **Digital/Crossfade** — two taps, ~20–500 ms crossfade; no click, **no pitch shift**. Downside: brief flam. Default for Digital/Diffuse.
  - **Tape/Repitch** — read-rate glides old→new; **pitch bends** (dub warble); glide-native, never clicks. Default for Tape/BBD.
  - **Time-stretch (pitch-constant)** — granular/PSOLA; premium, HQ-only. (FabFilter Timeless "unique time-stretch mode.") v2 stretch.
- **Feedback-path filter = the tonal heart [FACT].** Must be **inside** the loop so the filtering intensifies with each repeat (the dub sound). Provide **Low Cut (HP) + High Cut (LP)** or a bipolar Tilt. (Logic Tape Delay; Softube Echoes)
- **Diffusion [FACT]:** cascade of short allpasses in the loop smears transients into a lush reverb-like wash; **non-multiple spread times** + enough stages avoid metallic ring; g 0.5–0.7; more stages = smoother/longer smear. This is the delay→reverb morph and the Ghost engine.
- **Ducking [FACT]:** env-follower on the dry (attack ~5 ms, release 20–500 ms) reduces wet gain (or feedback only, subtler) so echoes bloom in gaps. External-sidechain option. Low CPU. Threshold + release controls. (Softube Echoes; u-he Colour Copy MODE OFF/FAST/MED/SLOW + TARGET AMP/FB.)
- **Freeze/Hold [FACT]:** set feedback=100% AND stop writing input → buffer loops forever; modulation still applies. Equal-power ~10–50 ms enter/exit crossfade (click-free). (FabFilter Freeze; u-he Colour Copy FREEZE.)
- **Self-oscillation [FACT]:** begins ~100% (Valhalla). Cap ~110–115%, hard-limit the loop, soft-clip so it saturates musically instead of exploding. Denormal-flush all four matrix multiplies.

---

## 7. Reference-Plugin Comparison Table

| Plugin | What it nails | Types/Modes | Signature mechanism | Source |
|---|---|---|---|---|
| **Serum 2** | The HQ interpolation upgrade; scalar dotted/triplet; inverted-Q bandpass wet filter | Normal / Ping-Pong / Tap→Delay + HQ toggle | Fractional-interp order switch (HQ default-on) → the phasey character | xferrecords; decembercymatics Serum manual |
| **Valhalla Delay** | The **Mode×Style×Diffusion orthogonality**; mode-specific "Age" knob; fbMix cross-morph | Modes: Tape/HiFi/BBD/Digital/Ghost/Pitch/RevPitch/LoFi · Styles: Single/Dual/Ratio/PingPong/Quad | Character block wraps one line; Ghost = freq-shift+diffusion in feedback | valhalladsp.com (mode/style/controls pages) |
| **Arturia (TAPE-201 / Memory-Brigade / Eternity)** | Authentic RE-201 multi-head + spring; BBD Size/companding; Eternity's **Time Mode Repitch/Digital/Fade** + per-line bit-crush/filter | Tape (12-head modes) / BBD (chorus-vibrato) / Digital (5 delay modes, dual+serial) | Repeat Rate = tape speed (pitch-bends); Motor Inertia = glide time-constant | Arturia official manuals (TAPE-201, Memory-Brigade, Eternity, Mello-Fi) |
| **Soundtoys EchoBoy** | **The "feel" engine** — Groove(swing/shuffle) + Feel(Draggin'/Rushin' pocket) + Accent + Prime Numbers anti-resonance; ~28 hardware Styles; per-repeat **EQ Decay** | 28 styles + Single/Dual/Ping-Pong/Rhythmic | Style Edit = per-band EQ Gain+Decay, Diffusion(Loop/Post), Wobble, Saturation types | soundtoys.com EchoBoy V5 manual |
| **u-he Colour Copy** | **BBD gold standard** — fixed-length line + varispeed clock (smooth pitch on time change); 5-colour compander morph; HQ latency handling | BBD/analog, Reso/Sparkle/Fuzz/Snap/Dusk colours | Compander feedback loop; RATE = clock speed not buffer resize | uhe-dl ColourCopy user guide |
| **u-he Twangström** | Physical-model **spring** (dispersion chirp/splash); Bright = internal-SR/HQ switch | 3 tank types (2/3-spring) | Dispersive allpass chain (highs arrive late); Coupling on/off = wash vs discrete | uhe-dl Twangstrom user guide |
| **Phase Plant / Multipass** | **Modular framing** — delay as a NODE, every param modulatable; separate Tape-Stop node; per-band Multipass | Clean delay + chained snapins | Character via chaining, not baked in (opposite of Colour Copy) | kilohearts.com docs |
| **FabFilter Timeless 3** | Three time-change models incl. pitch-constant; Freeze | Tape/Original/Time-stretch | Granular pitch-constant stretch | fabfilter.com/help/timeless |

---

## 8. Interactive + Audible Web-Audio MOCKUP Build Plan

Single self-contained `.html`, opened locally in **Safari (file://)** per the mockup-first hard rule. Max hears/approves before any JUCE code.

**Architecture.** One **AudioWorkletNode** does everything (matches recycle-existing + CPU laws). Worklet source = a JS template string → `Blob` → `URL.createObjectURL` → `ctx.audioWorklet.addModule(url)` (the standard trick; works from `file://` where an external `.js` path is awkward). Graph: `source → inputGain → AudioWorkletNode('delay-processor') → outputGain → AnalyserNode → destination`. **[FACT — MDN/web-audio-samples]**

**Critical gotchas [FACT]:**
- AudioContext starts `suspended` — `await ctx.resume()` inside a click handler (autoplay policy), **then** `await addModule()`, **then** construct the node. Guard so init runs once.
- `process()` must be **allocation-free** (pre-allocate ring `Float32Array`s in the constructor) or GC drops audio.
- Denormal guard (`+1e-20`) in the feedback accumulator; clamp feedback ≤0.98 in-worklet regardless of UI.

**Signal path in the worklet.** Static ring buffer (2 s × sampleRate, power-of-two, mask-wrap). Linear interp is fine for the mockup; add a cubic-Hermite reader as the "HQ" A/B. Feedback: `read → tone biquad(LP+HP in loop) → character block(branch on TYPE) → tanh soft-clip → feedbackGain → write`.

**Per-type in the mockup:**
- **Digital** — passthrough; crossfade two read taps on time change (two pointers, equal-power ~20 ms) so pitch stays put.
- **Tape** — wow(OscillatorNode ~0.7 Hz) + flutter(~8 Hz) + smoothed noise summed onto the read offset; tanh WaveShaper (`oversample:'4x'` for HQ); LP ~6 kHz in loop; 150 Hz head-bump peak. Tape-Stop button ramps a rate multiplier 1→0 (exp).
- **BBD** — LP whose freq = map(delayTime) both pre and in-loop; WaveShaper drive; fake compander (1-pole env on feedback drives a gain, compress-in/expand-out) + level-scaled noise for the pump.
- **Diffuse/Ghost** — 4–6 cascaded `BiquadFilter type='allpass'` (or short modulated delays) in the loop; frequency shift = worklet SSB (Hilbert I/Q × quadrature osc) — **worth the worklet because the inharmonic glissando IS the demo**; a ring-mod approximation is the fallback (flag as approximation).

**Routing.** Build the matrix explicitly (or inside the worklet): Normal `a=c=fb,b=d=0`; Ping-Pong `a=c=0,b=d=fb` + sum input to one side; **Cross-morph slider** `a=c=fb(1−x), b=d=fb·x`. Width via mid-side on the wet bus only (negative width swaps ping side). **Mono button** sums output to demo fold-down (sells the mono-compat story live).

**Sources (all three).** (1) internal Osc + a synthesized decaying pluck buffer + a big **Trigger** button (hit a stab, hear the tail); (2) drag-drop → `file.arrayBuffer()` → `decodeAudioData` → BufferSource (**decode in memory, no disk** — project rule); (3) mic via `getUserMedia`. Dry/Wet A/B toggle.

**Viz (everything-audible-interacts-visually rule).** Tap worklet output → AnalyserNode → `requestAnimationFrame`: scrolling scope + a decaying echo-tap meter; show the **spectrum darkening over repeats** and the **pitch wobble**; Ping-Pong draws L/R on split lanes. Thin-white strokes on near-black, purple accent on the active TYPE pill (v7 aesthetic).

**A/B demos to ship:** Digital-vs-Analog *on the same echo* (the exact Serum-HQ phasing question Max asked), the three time-change behaviors side by side, "same note through all types," a Cross-morph sweep (Normal→Ping-Pong), and a "Dotted Bounce" preset.

---

## 9. Proposed Param List for OUR Delay (mapped onto the chassis)

**Exactly 3 front params + Mix; 8 back knobs (4×2) + 2 dropdowns.** Every slot justified; nothing kept that isn't dramatic.

### FRONT (3 + Mix)
| Slot | Param | Range | Why it's on the front / dramatic |
|---|---|---|---|
| 1 | **Time** | 1 ms–2 s free, or synced 1/64…1/1 + DOT(×1.5)/TRIP(×2/3) | The primary gesture; glides on change (repitch or crossfade per type). |
| 2 | **Feedback** | 0–~110% (clamp 0.98, soft-clip loop) | 0 = single slap → near-runaway wash. Reveals each type's true character. |
| 3 | **Tone** | bipolar: LP 200 Hz … flat … HP 6 kHz (in-loop) | Feedback-path filter — LENGTH vs COLOR separation (Tone = color, Feedback = length; honors "params play their roles"). |
| — | **Mix** | 0–100% (100% = fully wet) | Standard; on a send it's 100% wet. |

### BACK (8 knobs, 4×2)
| Slot | Param | Range | Justification |
|---|---|---|---|
| 1 | **Spread / Offset** | 0–±50% of Time (or R=L+Δ ms) | Stereo/ping-pong width + Haas; the L/R time relationship. |
| 2 | **Width / Cross** | 0–150% (mid-side wet) OR the Valhalla cross-morph 0–1 | Dramatic at extremes (mono-collapse → super-wide); flips ping side. |
| 3 | **Mod Rate** | wow 0.5–2 Hz / flutter 6–12 Hz / sine 0.1–6 Hz | Tape=wow/flutter, BBD/Digital=sine. Speed of movement. |
| 4 | **Mod Depth** | 0–±8 ms | 0 = static, max = seasick/chorus. Night-and-day. |
| 5 | **Age / Character** *(mode-specific)* | 0–100% | Tape=noise+splice+sat, BBD=hiss+pump, Digital=bit-crush, Diffuse=shift-amount. Re-labeled per type (Valhalla law). |
| 6 | **Diffusion** | 0–100% (4–8 allpass) | Delay→reverb morph; feeds the Diffuse type; smear option for all. |
| 7 | **Ducking** | 0–100% (threshold+release folded) | Modern staple; keeps echoes out of the way then blooms. |
| 8 | **Shift** *(mode-specific)* | ±500 Hz (Diffuse) / repurposed | Ghost freq-shift inharmonic drift; the standout sound-design move. |

### DROPDOWNS (2)
- **Dropdown #1 = TYPE:** Digital / Tape / BBD / Diffuse. Re-voices device + re-labels knobs 5 & 8.
- **Dropdown #2 = ROUTING + SYNC:** Normal / Ping-Pong / Dual / Mono × {Free ms, 1/4, 1/8, 1/8T, 1/8D, 1/16}.

**Cuts made:** separate Freeze knob (make it a momentary/modulatable *button/state*, not a knob — over any type); separate Wow *and* Flutter knobs (merged into Mod Rate/Depth, split internally per type); EchoBoy Groove+Feel (powerful but 2 knobs we don't have budget for — **note as a v2 "Feel" macro** = combined swing + signed pre-delay); harmonic Pitch-shimmer type (v2); a dedicated HQ knob (make **HQ a small toggle/switch** in the header like the reverb device, not a back-panel knob — it's a quality gate, not a musical param). **[INFER]**

---

## 10. Open Questions / Risks / CPU Budget

**Open questions**
1. Is HQ a **global header toggle** (like reverb) or **per-type default-on**? Recommend header toggle + smart default (on for Digital/Diffuse where transparency matters, tape/BBD run at reduced internal rate authentically). Must handle **PDC/latency + dry-path delay** (Colour Copy trap).
2. **Ping-pong = routing** (recommended) vs a **type** (reads more dramatic to users). We recommend routing; verify Max agrees in the mockup.
3. Do we ship the **Cross-morph knob** *or* discrete routing modes? Recommend the knob framed as "Width/Cross" — spans the whole continuum with one control.
4. **Freeze** as button vs modulatable state — confirm UI real estate in v7 chassis.
5. **Shift knob** repurposing when not in Diffuse type — grey out, or map to a subtle detune for Tape/BBD?

**Risks**
- **Allpass interpolation phasing** — if we accidentally use allpass for the transparent default we reintroduce the exact Serum phasiness. Default MUST be linear-phase Lagrange/sinc; allpass only as deliberate character. **[FACT]**
- **Compander overshoot** (BBD) on fast tap-mod → buffer peaks; needs attack/release smoothing + limiter guard (u-he's own warning). **[FACT]**
- **Metallic ring** from diffusion at near-multiple times → non-multiple spread + enough stages. **[FACT]**
- **Feedback runaway / denormal CPU spikes** — clamp ≤0.98, soft-clip loop, flush denormals on every matrix multiply.
- **Frequency shifter mirror sideband** (Ghost) — a cheap Hilbert leaks the unwanted sideband as a faint ghost tone; needs ≥8–12 allpass sections / >60 dB rejection. **[FACT]**
- **HQ latency mismatch** → dry/wet phase cancellation if PDC not reported. **[FACT]**

**CPU budget (dynamic node chain).** Per JOS + Colour Copy/Twangström evidence:
- Core = 2 delay lines + 4 feedback multiplies + 4-mult M/S matrix + one 1-pole LP/HP per line = **trivial** (a handful of mults/sample).
- Cubic-Hermite read ≈ 4 mults/sample/line (cheaper per-quality than oversampling+linear).
- **Oversample ONLY** the nonlinear sub-blocks (tape/BBD saturation, freq/pitch shift) at 2×, and **only when engaged** (Digital needs none). Analog types can run the delay/tank at a **reduced internal rate by default** (authentic AND cheap — Twangström's Bright switch confirms downsampling is part of the sound, not a compromise).
- Diffusion (4–8 allpass) + Hilbert (Ghost) are the heaviest optionals → behind the Diffuse type / HQ toggle.
- Net: a clean delay is nearly free; a fully-loaded Ghost with HQ oversampling is the ceiling. Budget the node so **worst-case (Diffuse + HQ + high feedback)** still fits alongside the chained reverb on all 6 route buses.

---

*End of consolidated research. Sources cited inline; primary manuals: Xfer Serum 1/2, Valhalla (Delay/FreqEcho/ÜberMod blog + shop), Arturia (TAPE-201/Memory-Brigade/Eternity/Mello-Fi), Soundtoys EchoBoy V5, u-he (Colour Copy/Twangström), Kilohearts (Phase Plant/Multipass), FabFilter Timeless 3, JOS/CCRMA PASP + dsprelated, DAFx Raffel & Smith BBD, Ableton/Bitwig/UA time-update docs, Sweetwater comb-filter, MDN Web Audio / AudioWorklet.*
