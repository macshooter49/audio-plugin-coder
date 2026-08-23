# 00 — SPECTRAL / WAVETABLE INVENTORY (the shipped baseline)

**Read this before proposing a single new spectral mode.** Everything below is what is
*actually compiled into the plugin today*, read line-by-line from source, with `file:line`
citations. Where a number was measurable it was **measured** (harness at the bottom), not
estimated — several long-standing code comments in the tree are stale and are flagged as such.

Files inventoried:
- `Source/SpectralMorph.h` (306 lines) — the 7 morph modes
- `Source/Wavetable.h` (2018 lines) — storage, mips, `buildFromSpec`, `renderBlend`, `toSpec`, the FFT pair
- `Source/WavetableBank.h` (179 lines) — 30 factory tables + `specForPreset`
- `Source/SynthVoice.h` (6266 lines) — WARP, the blend read, mip selection
- `Source/Shapers.h` (169 lines) — the FOLD (extracted from SynthVoice, fb313)
- `Source/PluginProcessor.{h,cpp}` — `rebuildMorphIfNeeded`, `MorphSlot`, the 60 Hz timer
- `Source/GeodeEngine.h`, `Source/HarmonicEngine.h` — the *other* spectral machinery already in the box

---

## 0. The one-paragraph model of the whole system

A wavetable is **16 frames × 2048 samples × 34 band-limited mip levels** (`Wavetable.h:57-81`),
built **offline** by inverse-FFT from a frequency-domain `WavetableSpec`. The spectral morph is a
**pure, stateless function on that spec** (`WavetableSpec → WavetableSpec`), applied on the
**message thread**, whose output is re-synthesised into a whole new 34-mip table and
atomic-published to the voices. The audio thread never touches a spectrum: per block it picks a
mip, calls `renderBlend()` once per osc to flatten the frame axis into ONE 2048-point cycle, then
every unison sine reads that buffer with `readCycle()`. Everything the *audio thread* does to
timbre after that point is **time-domain**: phase remaps (WARP), amplitude shapers (WARP 9/10),
and the wavefolder.

```
 message thread (60 Hz timer)                          audio thread (per block → per sample)
 ────────────────────────────                          ─────────────────────────────────────
 specForPreset(p)  or  import->toSpec()                mipLevelForPhaseIncrement()   ← per block
        │  WavetableSpec (16 frames)                          │
        ▼                                              renderBlend(mip, framePos, blur, buf)  ← per block
 SpectralMorph::apply(spec, mode, amount)   0.02 ms           │  ONE 2048-pt cycle
        │  WavetableSpec                                      ▼
        ▼                                              applyPhaseWarp() → readCycle(buf, φ)   ← per SAMPLE
 Wavetable::buildFromSpec()   ~21 ms MEASURED                 │
        │  34 mips × 16 frames × 2048                         ▼
        ▼                                              applyAmpWarp() → applyFoldADAA() → unison sum
 slot.live.store(&buf[i])  ───── atomic ─────────────► wavetableForOsc()
```

---

## 1. The 7 spectral morph modes

**Where:** `SpectralMorph.h:39-50` (enum), `SpectralMorph.h:105-304` (the switch).
**Parameter (identical for all 7, per osc A/B/C/D):**

| | |
|---|---|
| Type | `SYN_OSC_{A,B,C,D}_SPECTRAL_TYPE` — `AudioParameterChoice`, 8 options, default 0 (`PluginProcessor.cpp:2345-2351`) |
| Options | `None · Harmonic Stretch · Inharmonic Stretch · Vocode · Smear · Random Amps · Data Compress · Spectral Phaser` |
| Amount | `SYN_OSC_{A,B,C,D}_SPECTRAL_AMT` — `AudioParameterFloat`, **0.0 … 1.0, step 0.001, default 0.0** (`PluginProcessor.cpp:2353-2357`) |
| UI mirror | `ui/public/index.html:19048` `SPECTRAL_MODES[]` — same 8 strings, same order |
| Modulation | dests `wc::ModDest::SpectralA..D` (`SynthModConfig.h:129`, domain `Linear01` `:516`); the audio thread publishes the modulated value to `spectralEffAmt_[osc]` each block (`PluginProcessor.cpp:7542-7557`) |

**The shared pre/post step, and it matters more than any individual mode.** Every mode runs on a
flat partial list produced by `extract()` (`SpectralMorph.h:76-95`) and written back by
`writeBack()` (`:97-103`). `extract()` reads whichever representation the frame used — integer
harmonics (`amplitudes[h-1]`, `phases[h-1]`) *or* the arbitrary `partials[]` list — and emits a
single `{ratio, amp, phase}` array. `writeBack()` sets `out.numHarmonics = 0`, which **forces
`buildFromSpec` down its inharmonic-partial path** (`Wavetable.h:126-155`), where each partial's
energy is split linearly (as a phasor sum) across the two adjacent integer harmonics. That snap is
what makes a non-integer ratio legal in a single-cycle looped frame.

### 1.1 `HarmonicStretch` (1) — spread + brighten
`SpectralMorph.h:112-131`. Two things happen at once. **Spread:** every partial's ratio is fanned
away from the fundamental, `r' = 1 + (r − 1)·s` with `s = 1 + 5.5·a` (`:122`), so `s` runs 1 → 6.5
and the series 1,2,3,4… becomes 1,7.5,14,20.5… at full. The fundamental is a fixed point.
**Brighten:** `amp *= r'^(1.15·a)` (`:123, :128`) — a positive-exponent tilt in ratio space, which
cancels the natural 1/h rolloff so the spectrum goes flat and buzzing instead of merely thinning
out (the comment records that without the tilt the RMS halved). At `a → 0` both `s → 1` and the
exponent → 0, so it is an exact identity in the limit — *modulo the truncation defect in §1.8*.
This is the only mode that both moves partials AND boosts them, and it is the loudest.

### 1.2 `InharmonicStretch` (2) — power-law spacing
`SpectralMorph.h:133-149`. `r' = r^p` with `p = 1 + 2.3·a` (`:140`), i.e. the exponent runs 1 → 3.3.
Because ratio 1 is a fixed point of any power, the fundamental is pinned and everything above
explodes outward super-linearly (h=2 → 2^3.3 ≈ 9.85 at full), which is the bell/gong/struck-metal
stretch. Same brightness tilt as §1.1 but slightly weaker: `amp *= r'^(1.05·a)` (`:141, :146`).
Distinct from `HarmonicStretch` in that the spacing is non-uniform — the gaps between adjacent
partials widen as you climb, which is exactly what makes it read as *inharmonic* and not as
*transposed*.

### 1.3 `Vocode` (3) — a formant envelope that sweeps the vowel
`SpectralMorph.h:151-184`. The only mode whose control axis is *not* depth-of-one-effect but
**position along a trajectory**. Five cardinal vowels are tabled as (F1,F2,F3) in Hz — male
register, Hillenbrand et al. 1995 (`:158-164`): /u/ 378/997/2343, /o/ 497/910/2459, /a/
768/1333/2522, /e/ 580/1799/2605, /i/ 342/2322/3000. `amount` is scaled `vIdx = a·4` (`:166`) and
the three formants are **linearly interpolated** between the two bracketing vowels (`:170-172`).
Each partial's frequency is taken as `f = r · F0` with a **hard-wired `F0 = 130.81 Hz` (C3)**
(`:165`) — the envelope does NOT track the played note. The envelope is a sum of three Lorentzians
(reusing `Wavetable::lorentzian`, §5.4): `env = 0.012 + 2.10·L(f,F1,72) + 1.55·L(f,F2,96) +
1.05·L(f,F3,130)` (`:177-180`), and the wet/dry law is `amp *= (1−a) + a·env` with `depth = a`
(`:173, :181`). So the vowel sweep and the depth ramp are welded to the same knob: you cannot hold
/a/ at 30% depth.

### 1.4 `Smear` (4) — rolloff + neighbour blur + phase scatter
`SpectralMorph.h:186-229`. Three stacked stages, all keyed to `a`.
**(i) Rolloff:** a 4th-order-ish soft LP *in harmonic-index space* — `roll = 1/(1 + (r/cut)^4)`
with `cut = 3 + (1−a)²·92` (`:196, :200`), so the corner falls ~95 → 3 harmonics; applied as
`amp *= (1−a) + a·roll` (`:201`). The comment is explicit that this rolloff, not the blur, is the
big audible move.
**(ii) Blur:** a **triangular** (Bartlett) moving average across the *partial index* axis, half-width
`W = round(11·a)` (`:203`), weights `w_k = 1 − |k|/(W+1)`, renormalised by the actual weight sum so
edges don't thin (`:210-219`). Reads from a snapshot `src[]` so it is a true convolution, not an IIR.
**(iii) Phase scatter:** xorshift32 seeded `0x5EED1234` (`:222-225`), `phase += a·4.1·rnd11()`
(`:227`). The seed is fixed and the RNG is re-seeded per frame, so scatter is deterministic and
stable across rebuilds.
Note the axis: (ii) blurs across **partial index**, whereas WT BLUR (§3) blurs across the **frame**
axis. They are orthogonal.

### 1.5 `RandomAmplitudes` (5) — a fixed-seed gain re-roll
`SpectralMorph.h:231-250`. xorshift32 seeded `0x0A17C0DE` (`:238`), one draw per partial index, so
the mutation is a **per-preset signature**, not noise, and it is identical on every frame and every
rebuild. Multiplier `mul = 0.02 + r²·5.5` (`:245`) — squared, so most partials are pulled *down*
and a few are thrown hard up (range 0.02 … 5.52). The fundamental is protected: any partial with
`ratio ≤ 1.01` gets `mul = 0.7 + 0.3·r` instead (`:246`), so pitch never disappears. Wet/dry
`amp *= (1−a) + a·mul` (`:247`). Ratios and phases are untouched — this is the only mode that acts
purely on magnitude.

### 1.6 `DataCompress` (6) — a frequency-domain bitcrusher
`SpectralMorph.h:252-275`. **The only mode that removes information.** Two mechanisms.
**Quantise:** amplitudes are normalised by the frame's peak `maxA` (`:259-260`), rounded to
`levels = max(2, round(64 − 62·a))` steps (`:261`), then re-scaled: `amp = round(amp/maxA·L)/L·maxA`
(`:271-272`). **Decimate:** `keepEvery = 1 + round(3·a)` (`:262`); any partial whose index `i ≠ 0`
and `i % keepEvery ≠ 0` is zeroed (`:266-270`) and then skipped entirely by `buildFromSpec`'s
`amp == 0` fast path. At `a = 1` that keeps 1 partial in 4 and quantises the survivors to 2 levels.
Index 0 (the fundamental as extracted) is always kept. Note the decimation is on **list index**,
not on harmonic number — for an inharmonic source (D50Bell, GlassHarmonics) it thins whichever
partials happen to sit at those slots.

### 1.7 `SpectralPhaser` (7) — a comb of notches that slides
`SpectralMorph.h:277-295`. `notch = |sin(π·(r + sweep)/period)|²` with `period = 1.7` (fixed,
`:285`) and `sweep = 4.5·a` (`:286`); applied `amp *= (1−a) + a·notch` (`:292`). So opening the
knob does two things simultaneously — the notches get **deeper** (via `depth = a`) and **slide up**
the harmonic axis (via `sweep`). Squaring the sine (`:291`) widens/deepens the troughs vs a plain
sine. With an LFO on `amount` the notches march, which is the actual phaser motion; but because
depth and sweep share the knob, an LFO also breathes the depth.

### 1.8 🚨 THE SHARED CONSTRAINT NOBODY WROTE DOWN — `kMaxPartials = 96`

`FrameSpec::kMaxPartials = 96` (`Wavetable.h:49`) but `FrameSpec::kMaxHarmonics = 512`
(`Wavetable.h:35`). `extract()` stops at 96 (`SpectralMorph.h:81, :87`). Therefore **the instant
`amount` leaves 0 with any mode selected, every component above the 96th is discarded** — not
attenuated, *deleted* — and since `apply()` short-circuits at `amount <= 0` (`:62-63`), the
transition is a **step, not a ramp**.

Measured (this session, harness in §6) — worst frame of each affected factory table, morphed at
`amount = 1e-6`, nulled against the unmorphed table:

| preset | max components in a frame | null at amount = 1e-6 |
|---|---:|---:|
| Pulse | 511 | **−18.2 dBr** |
| Square | 511 | **−21.6 dBr** |
| Triangle | 256 | **−25.1 dBr** |
| Rise | 128 | **−9.4 dBr** |
| SpectralSweep | 198 | **−8.7 dBr** |

25 of the 30 factory tables are under the cap and are unaffected. This is a real, audible
discontinuity on 5 tables and it is a **hard ceiling on any new mode**: no new mode can be richer
than 96 partials per frame without raising `kMaxPartials` (which also grows `FrameSpec` — currently
`96 × 12 B = 1.1 KB` of partials on top of `2 × 512 × 4 B = 4 KB` of harmonic arrays, per frame,
× 16 frames per spec).

### 1.9 The second shared constraint — the morph is always 16 frames

`apply()` iterates `WavetableSpec::kNumFrames` = **16** (`SpectralMorph.h:66`, `Wavetable.h:59`),
and `buildFromSpec` sets `numFrames_ = 16` (`Wavetable.h:103`). Imports can hold up to
`kMaxFrames = 256` (`Wavetable.h:69`), and `toSpec()` resamples them to 16 by **nearest source
frame** (`Wavetable.h:277-279`). So **engaging any spectral mode on a 256-frame import collapses
its frame axis 256 → 16.** The raw import still plays at full resolution when the morph is off
(`wavetableForOsc`, `PluginProcessor.h:1444-1455`) — so this too is a step change at `amount = 0+`.

---

## 2. How the morph is applied — offline table bake, on the message thread

**Answer: an offline table bake, throttled to at most ~20 Hz, never per block, never on the audio thread.**

### 2.1 The call site and cadence
`startTimerHz (60)` (`PluginProcessor.cpp:266`) → `timerCallback()` (`PluginProcessor.cpp:939`)
calls `rebuildMorphIfNeeded` once per osc (`:961-972`) for A/B/C/D, passing that osc's
`WT_PRESET` / `SPECTRAL_TYPE` / `SPECTRAL_AMT` ids. So the *check* runs at 60 Hz; the *build* is
gated (§2.3) to at most one every 3 ticks ≈ **20 Hz**.

### 2.2 What `rebuildMorphIfNeeded` does — `PluginProcessor.cpp:824-916`
1. **Read the type** as a raw choice index; **read the amount from `spectralEffAmt_[osc]`**, the
   LFO/env-modulated value the audio thread publishes each block, falling back to the raw APVTS
   param when the audio thread has not run yet (sentinel −1) (`:836-837`).
2. **Resolve the source spec** (fb253): if an import is live for this osc, use a **cached**
   `imp->toSpec()` re-derived only when the import pointer or `buildEpoch` changes
   (`:882-892`); otherwise `WavetableBank::specForPreset(preset)` (`:894-897`).
3. **`mode <= 0 || amount <= 0` → publish `nullptr`** and arm a 2-tick retire cooldown
   (`:845-858`). Voices then fall back to the raw import, else the bank table.
4. **Change gate:** skip entirely if source identity (import pointer + epoch, else preset index),
   mode, and amount (within `1e-4`) are all unchanged (`:861-867`).
5. **Retire cooldown / buffer safety** (`:872-882`): never rebuild the live buffer, never one the
   audio thread reports reading (`audioReadingIdx`), and burn 2 ticks after any publish so
   in-flight blocks can leave a just-retired buffer.
6. **Build:** `ready[target] = false` → `buf[target].buildFromSpec(SpectralMorph::apply(*srcSpec,
   mode, amount))` → `ready[target] = true` → `live.store(&buf[target])` (`:903-909`).

### 2.3 The publish contract — `MorphSlot`, `PluginProcessor.h:1354-1375`
Two `tw::Wavetable` buffers, an atomic `live` pointer, an atomic `audioReadingIdx` (the audio
thread's claim), and per-buffer atomic `ready[2]`. `ready[]` exists because `buildFromSpec` zeroes
and refills **in place**: without it a voice could render a blend composite from a half-built
(zeroed) table and, with pointer-keyed caching, latch **silence** indefinitely. That is also why
`Wavetable::buildEpoch()` exists (`Wavetable.h:296-305`) and why the voice's blend cache keys on it
(`SynthVoice.h:2586, 2591`). The read side is split in two on purpose: `wavetableForOsc()`
(`PluginProcessor.h:1435-1456`) writes `audioReadingIdx` and is **audio-thread only**;
`wavetableForDisplay()` (`:1423-1433`) is a read-only twin for the UI so the editor cannot forge
the audio thread's claim (fb459).

### 2.4 🚨 The cost — MEASURED, and the code comment is stale
`PluginProcessor.h:1347-1349` says *"SpectralMorph::apply + buildFromSpec is ~5.6 ms"*. That number
dates from the 8-mip era; `kNumMipLevels` is now **34** (`Wavetable.h:67`, fb301). Measured on this
machine (Apple silicon, `-O2`, ProphetSaw):

| operation | measured |
|---|---:|
| `SpectralMorph::apply` (all 7 modes are cheap; Smear measured) | **0.023 ms** |
| `Wavetable::buildFromSpec` (34 mips × 16 frames × 2048-pt iFFT = **544 iFFTs**) | **20.8 – 23.1 ms** |
| `Wavetable::toSpec` (16 forward FFTs) | **0.88 ms** |
| `renderBlend`, blur = 0 (fast path) | **0.16 µs** per call |
| `renderBlend`, blur = 1, 16-frame table | **13.2 µs** per call |

So one osc sweeping its Spectral knob costs ≈ 21 ms every ≈ 50 ms of message-thread time — **~40%
duty on the message thread, per osc**. Four oscs sweeping at once saturates it and will visibly
stall the UI. **Any proposal that increases `buildFromSpec` work (more mips, more partials, more
frames) multiplies this directly**, and any proposal that needs faster-than-20 Hz response cannot
go through the bake path at all.

---

## 3. WT BLUR — what `renderBlend` exactly does

**Where:** `Wavetable.h:341-441`. Param `SYN_OSC_A_FRAME_SPREAD` renamed "OSC A Blur",
float **0..1, step 0.001, default 0** (`PluginProcessor.cpp:2369-2373`); pushed via
`setBlur`/`setBlurCD` (`SynthVoice.h:1172-1176, :1657`), smoothed per block with a one-pole
`blur += (target − blur)·0.25` (`SynthVoice.h:2568-2576`); mod dests `ModDest::BlurA..D`
(`SynthModConfig.h:130, :520`).

### 3.1 It is a Gaussian weighted MEAN over the FRAME axis, RMS-matched — therefore subtractive only

The output is a **convex combination of frames of the same mip level**. Weights sum to exactly 1:
`(1 − blur)` on the bilinear reference read, plus `blur` distributed over the Gaussian taps
(`Wavetable.h:416-418, :430`). Because every summed frame comes from the **same mip**, they share a
band edge, so the sum is band-limited by construction — **no new partials, no imaging, ever**. Per
harmonic bin the result is a weighted average of complex phasors, whose magnitude can only be ≤ the
weighted average of the magnitudes. **Blur can only cancel, never create.** The trailing RMS match
restores broadband level (`:437-440`), which makes the loss read as *timbral*, not as *quieter* —
but it does not put energy back into any harmonic.

### 3.2 The formula, exactly as shipped

Let `N = numFrames_`, `fIdx = clamp01(framePos)·(N−1)`, `f0 = ⌊fIdx⌋` clamped to `[0,N−1]`,
`f1 = min(f0+1, N−1)`, `fFrac = fIdx − f0`, `lvl = clamp(mip, 0, numMipLevels_−1)`, and
`x[f][n] = sample(lvl, f, n)`.

**Reference (the un-blurred bilinear frame read):**

```
ref[n] = x[f0][n]·(1 − fFrac) + x[f1][n]·fFrac
```

**Fast path** — `blur ≤ 1e-4` (`:365-376`): `out[n] = ref[n]`, return immediately, **no RMS match**
(the reference *is* the reference). This is O(frameSize) and independent of `N`, which is what makes
WT-Pos modulation affordable on a 256-frame import.

**Blur path** (`:378-440`):

```
σ      = 1e-4 + N · 1.05 · blur^1.25                        (:394)
band   = min(N, ⌈4σ⌉ + 2)                                   (:395)
lo     = max(0, f0 − band),  hi = min(N−1, f1 + band)       (:396-397)
span   = hi − lo + 1
stride = max(1, ⌈span / kBlurMaxTaps⌉),  kBlurMaxTaps = 32  (:403-404, :68)

taps   T = { lo, lo+stride, lo+2·stride, … ≤ hi }
g_f    = exp( −½ · ((f − fIdx)/σ)² )            for f ∈ T   (:411-413)
w_f    = blur · g_f / Σ_{j∈T} g_j                           (:416-418)

pre[n] = Σ_{f∈T} w_f · x[f][n]  +  (1 − blur) · ref[n]      (:425-431)

G      = sqrt( Σ_n ref[n]²  /  Σ_n pre[n]² )                (:437-438)
out[n] = G · pre[n]                                         (:440)
```

Three details that are load-bearing and easy to get wrong:
- **σ scales with `N`** (fb464). Blur therefore means the same *fraction of the table* on a 16-frame
  factory table and a 256-frame import. At `blur = 1`, `σ ≈ 1.05·N` → the kernel is essentially flat
  across the **whole table**: blur 100% = the uniform mean of every frame. That is the mathematical
  maximum a frame-axis mean can do.
- **The exponent is 1.25, not 2** (fb464), because the old `blur²·9` *absolute-frame* spread measured
  −53…−102 dB of harmonic change at 25% and only −13.5 dB at 100% (`Tests/blur_audit.cpp`) — the
  bottom half of the knob was inaudible. Max: *"blur isn't really doing much, like not at all."*
- **The bilinear part is added separately, not folded into `w[f0]`/`w[f1]`** (`:420-423`). With a
  stride, `f0`/`f1` need not land on the sampled grid, and a weight written to an index the
  accumulation loop never visits would silently vanish.

### 3.3 Cost ceiling and how the voice consumes it
`kBlurMaxTaps = 32` turns an O(frameSize × N) worst case into a fixed O(frameSize × 32) — this is
precisely what makes blur *modulatable* at all (fb75 had excluded it on CPU grounds). Measured:
13.2 µs per call at blur = 1 on a 16-frame table; a 256-frame import is bounded at 32 taps ≈ 2× that.
The voice calls `renderBlend` **once per block per osc**, gated on
`(framePos, blur, mipLevel, tablePointer, buildEpoch)` (`SynthVoice.h:2578-2593` for A, `:2595-2632`
for B/C/D), keeps the previous composite in `blendPrev*`, and crossfades prev→new across the block
via `wtBlendRead()` (`SynthVoice.h:6055-6059`) so a moving WT Pos doesn't step at block boundaries
(fb248). Every unison sine then reads that one buffer with `Wavetable::readCycle` — **linear**
interpolation, deliberately (fb302 measured Catmull-Rom at −130…−170 dB vs linear's −98…−115 dB and
reverted it as unhearable for 3× the cost) (`Wavetable.h:445-459`).

---

## 4. The WARP modes

**Where:** `SynthVoice.h:938-1000` (`applyPhaseWarp`, phase domain) and `:1003-1015`
(`applyAmpWarp`, amplitude domain). **11 options (0 = NONE + 10 modes)**, per osc, in **two chained
slots**: `SYN_OSC_x_WARP_MODE`/`_WARP_AMOUNT` and `SYN_OSC_x_WARP2_MODE`/`_WARP2_AMT`
(`ParameterIDs.hpp:519-522`), each amount float 0..1 step 0.001 default 0
(`PluginProcessor.cpp:2253-2256, :2331-2339`). Slot 2 runs **in series on slot 1's output**
(Serum parity) — phase warps chain phase-first (`SynthVoice.h:2795-2798`), amp warps chain after the
table read (`:2811-2812`).

All of it is **per-sample, audio thread, time domain.** `p` is the phase in `[0,1)`; `a` is the
amount; `window` is a multiplicative post-lookup gain (starts at 1.0 and *multiplies*, so slots
compose); `skipLookup` ORs (once any slot mutes a sample, it stays muted).

| # | Name | Domain | Maths as shipped | line |
|---|---|---|---|---|
| 0 | NONE | — | `p` passthrough | `:998` |
| 1 | **Bend** | phase | `p' = frac( p + a·0.5·sin(2π p) )` — a sinusoidal phase warp, ±0.5 cycle at full | `:942-948` |
| 2 | **Sync** | phase | `p' = frac( p · 2^(4a) )` — exponential hard sync, **1× … 16×** (Vital-style) | `:949-954` |
| 3 | **Formant** | phase + window | same `p' = frac(p·2^(4a))` as Sync, **plus** `window *= sin(π p)` — a half-sine bell keyed off the *input* phase, i.e. windowed sync | `:955-962` |
| 4 | **PWM** | phase + gate | `duty = max(0.10, 1 − 0.45a)`; if `p ≥ duty` → `skipLookup = true` (output 0); else `p' = p/duty` | `:963-968` |
| 5 | **Skew** | phase | piecewise-linear peak shift with `knee = max(0.05, 0.5 − 0.4a)`: `p<knee → p/knee·0.5`, else `0.5 + (p−knee)/(1−knee)·0.5` | `:969-974` |
| 6 | **Mirror** | phase | `m = (p<0.5) ? 2p : 2−2p`; `p' = frac( p·(1−a) + m·a )` — blend toward a squeezed mirror | `:975-981` |
| 7 | **Fractalize** | phase | `p' = frac( p·(1 + 7a) )` — fmod cascade, **N = 1…8** repeats per cycle | `:982-987` |
| 8 | **P-Quantize** | phase | `steps = round(2^(5 − 4a))` (**32 → 2**, never 1); `p' = (⌊p·steps⌋ + 0.25)/steps` | `:988-997` |
| 9 | **Rectify** | **amp** | `rect = \|s\|·2 − 1`; `y = s·(1−a) + rect·a` | `:1005-1009` |
| 10 | **Sine Shaper** | **amp** | `y = sin( s · (π/2) · (1 + 4a) )` — drive 1…5 | `:1010-1014` |

Two non-obvious facts:
- **The `+0.25` in P-Quantize is not cosmetic** (`:988-996`). A centre offset (`+0.5`) sits exactly
  on 0.25/0.75, which are the odd-harmonic **nulls** of the cosine-phase tables (Square/Pulse/Moog
  use `cosPhase = π/2`, `Wavetable.h:665, :698, :855`). It made P-Quantize go **silent** at low even
  step counts. A quarter offset is grid-misaligned with both the cosine nulls (0.25/0.75) and the
  sine nulls (0/0.5), so no table convention can zero every sample point.
- **Warp feeds the anti-aliasing decision.** `warpRateMul` (`SynthVoice.h:2428-2433`) returns
  `2^(4a)` for Sync/Formant and `1 + 7a` for Fractalize, and multiplies into the phase increment
  handed to `mipLevelForPhaseIncrement` (`:2558-2561`). A phase remap that runs the table faster
  must pick a darker mip or it aliases. **Any new phase-domain mode must declare its rate multiplier
  here or it will alias.** Modes 1/5/6/8/9/10 return 1.0 (they don't raise the read rate).

### 4.1 The FOLD (shipped separately, `Shapers.h`)
`SynthVoice.h:4789-4806` are thin forwards to `tw::shapers` (extracted verbatim, fb313, so the
oscillator fold and the FX-rack Distortion FOLD family share one implementation). Param:
`SYN_OSC_x_FOLD_SHAPE` choice `{Linear, Sine, Triangle}` + `SYN_OSC_x_FOLD_AMT` float 0..1
(`PluginProcessor.cpp:2359-2367`). Applied post-engine, per unison sine (`SynthVoice.h:2919`).

Pre-gain is **quadratic and lives in exactly one table** — `kFoldPre[3] = {9.0, 5.28318530, 5.0}`
(`Shapers.h:38`), `pre = 1 + a²·kFoldPre[shape]` (`:40-44`). That single-source rule exists because
the constants were previously duplicated between the shaper and its antiderivative, and a mismatch
silently breaks the ADAA identity (louder *and* more aliased at once — the "back it off" trap).

```
Linear (Serge)     q = (x·pre + 1)/4 ;  y = 4·|q − round(q)| − 1          (:56-63)
Sine   (Vital)     y = sin(x · pre)                                        (:64-69)
Triangle (Buchla)  y = 0.50·L(x·pre) + 0.35·L(x·pre·√2) + 0.15·L(x·pre·2)  (:70-84)
                   where L is the Linear fold above
```

Anti-aliased by **1st-order ADAA**: `y[n] = (F(x[n]) − F(x[n−1]))/(x[n] − x[n−1])`
(`Shapers.h:137-166`), with `F = foldAntideriv` (`:101-113`) built on
`G(q) = 2r|r| − r, r = q − round(q)` and `F_lin(x,a) = (4/a)·G((xa+1)/4)` (`:90-99`). A midpoint-naive
fallback covers the low-slew 0/0 case (`:150-153`), and `F(x[n−1])` is cached but **only while
`(shape, amount)` are unchanged** (`:155-158`). ⚠️ Note for anyone touching this: the voice ramps
fold amount **per sample** (`SynthVoice.h:2725`, `foldAmountA_ += foldStepA_`), so during any ramp
the cache guard misses and the fold costs **two** antiderivative evaluations per sample, not one.

### 4.2 The SAMPLE/GRAN/SPEC/HARM/MODAL warp (a different, smaller list)
`SynthVoice.h:2955-2971`. A separate 5-option shaper for the non-wavetable engines, reusing the
same primitives: `1` Sine Shaper (`applyAmpWarp(10)`), `2` Rectify (`applyAmpWarp(9)` + DC block),
`3` Fold (`applyFoldADAA(shape 0)`), `4` Drive (`tanh(s·(1+9a))` wet/dry), `5` Crush
(`round(s·L)/L`, `L = max(4, 64 − 60a²)`). Do not confuse this list with the WT WARP list — the
indices differ.

---

## 5. Spectral primitives that already exist and are reusable

### 5.1 Offline FFT / iFFT — `Wavetable.h` (private, build-time only)
- `Wavetable::inverseFFT (std::vector<std::complex<double>>&)` — `Wavetable.h:1956-1983`. In-place
  iterative radix-2, **raw / unnormalised**: `a[n] ← Σ_k a[k]·e^{+i2πkn/N}`. N must be a power of 2.
- `Wavetable::forwardFFT (…)` — `Wavetable.h:1947-1953`. Implemented via the conjugation identity
  `F = conj(inverseFFT(conj(x)))`, also raw/unnormalised, so a round trip needs an explicit `1/N`.
- **Bin convention you must match** (`Wavetable.h:165-190`): `X[h] = (A_h/2)·(sin φ_h − i·cos φ_h)`
  with the conjugate mirror at `N−h`, which makes the raw inverse yield exactly
  `Σ_h A_h·sin(2π h n/N + φ_h)`. The exact inverse is documented in `toSpec()`
  (`Wavetable.h:261-292`): with `c = X_fwd[h]/N`, `A_h = 2|c|`, `φ_h = atan2(Re c, −Im c)`.
  This round-trips to machine precision — verified in-tree and re-verified this session.
- ⚠️ Both are `private:` and both are **build-time only** (they allocate via `std::vector` at the
  call site and are O(N log N) in `double`). Nothing real-time may call them.

### 5.2 The spec ⇄ table bridge
- `Wavetable::buildFromSpec (const WavetableSpec&)` — `Wavetable.h:101-200`. The bake. Also
  contains the **energy-preserving inharmonic snap** (`:126-155`): a partial at ratio `R` deposits
  `(1−frac)` into harmonic `⌊R⌋` and `frac` into `⌊R⌋+1` **as phasors** (`px/py` accumulate
  `amp·w·cos φ` / `amp·w·sin φ`, then `a = |·|`, `φ = atan2`). Reuse this rather than reinventing
  fractional-harmonic placement.
- `Wavetable::toSpec()` — `Wavetable.h:267-292`. Analyse **any** table (imports included) into a
  16-frame harmonic spec. 16 forward FFTs, measured **0.88 ms**. Message thread only. This is the
  door that lets a spec-domain mode act on user content.
- `Wavetable::buildFromPcm (pcm, n, framesWanted)` — `Wavetable.h:204-259`. Forward-FFT each of
  `framesWanted` (≤ 256) windows, keep harmonics 1..hMax per mip, drop DC and Nyquist, inverse-FFT.
  This is the "turn anything into a wavetable" path.
- `WavetableBank::specForPreset (int)` — `WavetableBank.h:138-176`. The 30 factory specs, the single
  source of truth mirroring the bank constructor.

### 5.3 Band-limiting / mip machinery
- `kMipMaxHarmonics` — `Wavetable.h:79-81`. **34 levels**, sixth-octave ladder
  512→456→…→16 then 8/4/2. Must stay monotonically decreasing (selection scans front→back).
- `Wavetable::mipLevelForPhaseIncrement (double)` — `Wavetable.h:472-482`. Picks the first level
  whose harmonic cap ≤ `0.5 / phaseInc`. `mipLevelForMidiNote (note, sr)` — `:484-490`.
- `Wavetable::normalizeMipLevels()` — `Wavetable.h:1992-2008` (private). Peak-normalises **each mip
  level independently** to 1.0, so levels don't drift as harmonics are dropped. Note: this is
  **peak**, not RMS — a mode that changes crest factor will change perceived loudness.
- Memory cost of the ladder: 34 × 16 × 2048 × 4 B = **4.46 MB per table**, ~134 MB across 30
  factory tables (`Wavetable.h:67`).

### 5.4 Windowing / envelope helpers
- **There is no analysis window anywhere in the wavetable path.** `buildFromSpec` and `buildFromPcm`
  take rectangular full-cycle windows — legitimate, because every frame is exactly one period and
  harmonic `h` lands exactly on bin `h`. Do not add a Hann window to that path.
- `Wavetable::lorentzian (f, Fc, BW)` — `Wavetable.h:1489-1493`. `(BW/2)² / ((f−Fc)² + (BW/2)²)`.
  The house formant primitive; already used by `Vocode` (`SpectralMorph.h:178-180`) and by
  ChoirAtoO / Whisper / VowelMorph (`Wavetable.h:1538, :1593, :1656`).
- `Wavetable::besselJ (n, x)` — `Wavetable.h:1344-…`. Series Bessel of the first kind — the FM
  sideband amplitudes, used by the DX7EP spec. Reusable for any FM-in-the-spectrum mode.
- Gaussian bumps in harmonic space are written inline in several makers (e.g. `SpectralSweep`
  `Wavetable.h:1206-1216`, `SerumHD` `:1926-1936`, `PPGWave` `:1312-1331`) — **not** factored into a
  shared helper. If a new mode wants one, factoring it is cheap and overdue.
- `Wavetable::amplifyFramesInPlace (AmplifyMode)` — `Wavetable.h:514-587`. Time-domain per-frame
  variation (Warmth / Brightness / Drive / Spectrum) with a 1-pole split at α = 0.10 and `tanh`
  normalised by `tanh(drive)`. **Operates on mip level 0 only** — legacy path, not usable on
  spec-built (34-mip) tables.

### 5.5 Phase handling — the conventions you must not break
- Frames are `sin`-convention (`Wavetable.h:165-190`). Sine-phase tables null at φ = 0/0.5;
  cosine-phase tables (`cosPhase = π/2`, used by Square/Pulse/Moog) null at 0.25/0.75. **Both
  conventions coexist in the bank** — this is what bit P-Quantize (§4).
- The partial snap sums **phasors**, so phase is preserved through the inharmonic→harmonic snap
  (`Wavetable.h:138-152`).
- `Smear` is the only morph mode that touches phase (`SpectralMorph.h:226-227`); every other mode
  passes `p[i].phase` through untouched.
- `SpectralDrift` (`Wavetable.h:1884-…`) is the existing proof that **phase-only** change is
  audible on a wavetable: identical amplitude spectrum, phases drift 0 → random across the frame
  axis. Relevant precedent for a phase-domain mode — and a reminder that
  `feedback-perceptual-test-harness-hardrule` **bans sample-difference RMS** as a dramaticism
  metric precisely because phase-only changes score huge on it.

### 5.6 Real-time FFT machinery that already exists elsewhere (NOT in the wavetable path)
- `SpectrumAnalyzer.h:23-101` — `juce::dsp::FFT` order 12 (**4096**), hop 1024 (75% overlap),
  `juce::dsp::WindowingFunction<float>::hann`, `performFrequencyOnlyForwardTransform`, circular
  buffer, lock-free push to the UI. This is the **only** JUCE FFT in the synth path and it is
  analysis-for-display only.
- `GeodeEngine.h` — `geodedsp::fft (std::complex<float>*, n, inverse)` (`:140-165`, float, normalises
  on inverse, unlike `Wavetable`'s), `geodedsp::hann()` (`:168-178`, cached 2048-pt), `detectF0`
  NSDF autocorrelation (`:180-215`). `GeodeAnalyzer::analyzeSample` (`:222-262`) is a full
  **STFT peak-track resynth analyser**: `kWin = 2048`, `kHop = 512`, `kBins = 1025`
  (`:52-55`), `detectPeaks` with a median-threshold + 5-point local-max test and **centroid bin
  interpolation** over ±3 bins (`:303-328`), a birth/death `Tracker` for stable partial slots
  (`:331-376`), and a 16-band residual-noise envelope (`:377-395`). Output is
  `GeodeFrameStore` = up to 256 frames × 96 partials of `{ratio, amp}` — **the same 96 cap and the
  same ratio-to-fundamental representation `FrameSpec::Partial` uses.**
- `BlendEngine.h:295-370` — a third radix-2 `fft` + `hann` (kWin/kHop 2048/512, 75% OLA), offline.
- ⚠️ **Three separate hand-rolled radix-2 FFTs now exist in this tree** (`Wavetable.h`,
  `GeodeEngine.h`, `BlendEngine.h`) with **different normalisation conventions**. Any new spectral
  work should pick one deliberately and say which; adding a fourth is a defect.

### 5.7 Additive spectral transforms already shipped in `HarmonicEngine.h` (Engine::HARM)
Worth reading before designing new morph modes, because several ideas are **already implemented**
there in the real-time additive domain (up to 512 partials, `harm::kMaxPartials`), which means a
duplicate in `SpectralMorph` would be a "no doubles" violation:
`applySculpt` (`HarmonicEngine.h:692-808`) — `KEEL` pivot tilt (`:699`), **`SPLAY` anchored
inharmonic stretch** (`:714`, overlaps §1.2), **`CULL` sieve chain** odd/primes/Fibonacci (`:732`,
overlaps §1.6's decimation), `TIDE` traveling ripple (`:749`), **`TERRACE` dB-quantise the
spectrum** (`:760`, overlaps §1.6's quantiser), `CLANG` sum/difference intermod lattice (`:777`).
Plus reusable infrastructure: `applyNyquistTaper` (`:947`, a **smoothstep** 0.40–0.48·fs taper, not
a hard gate), `thinToBudget` keep-loudest (`:960`), `renorm` with a τ≈200 ms smoothed gain (`:984`),
and `sineLUT`/`sineAt` (`:460-476`).

---

## 6. Reproducing the measurements in this document

The numbers in §1.8 and §2.4 were measured this session. `Wavetable.h` includes
`<juce_core/juce_core.h>` for `jlimit/jmax/jmin` only, which `Tests/shim` does not provide, so a
3-function shim is needed alongside it:

```bash
# shim/juce_core/juce_core.h  →  namespace juce { jmax, jmin, jlimit }
clang++ -std=c++17 -O2 -I <shim> -I Tests/shim -I Source bench.cpp -o bench
```

Then time `Wavetable::buildFromSpec`, `SpectralMorph::apply`, `Wavetable::toSpec` and
`Wavetable::renderBlend`; and for §1.8, null `buildFromSpec(spec)` against
`buildFromSpec(SpectralMorph::apply(spec, mode, 1e-6f))` per frame at mip 0.

---

## 7. The constraint list every recommendation must satisfy

1. **96 partials per frame, hard** (`FrameSpec::kMaxPartials`, `Wavetable.h:49`) — and today the cap
   bites as a *step* at `amount = 0+` on Pulse / Square / Triangle / Rise / SpectralSweep
   (−8.7 … −25.1 dBr, measured).
2. **16 frames per spec, hard** (`WavetableSpec::kNumFrames`, `Wavetable.h:59`) — a 256-frame import
   collapses to 16 the moment any mode engages.
3. **The bake costs ~21 ms** and is throttled to ~20 Hz per osc on the **message thread**. Nothing
   in the morph path may run on the audio thread. Anything needing per-block response must be built
   somewhere else entirely (the `renderBlend` per-block hook or a `HarmonicEngine`-style additive
   path).
4. **`apply()` must stay pure, stateless, and amount-parameterised** — that contract is what lets
   `spectralEffAmt_` drive it from the mod matrix (`SpectralMorph.h:11-20`,
   `PluginProcessor.cpp:7542-7557`).
5. **`amount = 0` must be an exact identity**, and the approach to 0 must be continuous — see (1).
6. **Band-limiting is not optional.** Output must go back through `buildFromSpec` (which caps at
   `hMax` per mip) or, if it acts in the time domain, must declare a rate multiplier in
   `warpRateMul` (`SynthVoice.h:2428-2433`).
7. **Blur can only subtract** (§3.1). Any "richer/brighter" claim about the frame axis is false by
   construction; brightness has to come from the spec domain or a shaper.
8. **No doubles** — `SPLAY` / `CULL` / `TERRACE` / `CLANG` already exist in `HarmonicEngine.h`, and
   `Vocode` already owns the formant lane (as do three factory vocal tables).
9. **Peak-normalisation per mip** (`Wavetable.h:1992-2008`) means crest-factor changes read as
   loudness changes. Verify with the perceptual harness (magnitude-spectrum / centroid / HF-ratio),
   never with sample-difference RMS.
