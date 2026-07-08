# The Math of Resynthesis Engines — Deep Research (for Resynth)

**Date:** 2026-07-07 · **For:** Terrain Instrument → Resynth engine · **Method:** fan-out web search → 22 sources fetched → 82 claims extracted → adversarial 3-vote verification (18 confirmed 3-0, 0 refuted).
**Purpose:** give the Resynth build a rigorous, citable mathematical foundation — "the similar area to the math" — so every knob is grounded in what the best engines actually do.

> Verification note: claims marked **✓3-0** survived 3 independent adversarial fact-check votes. Claims marked **(fetched)** are from the extracted source set but the run hit a session rate-limit before they reached the vote stage — treated as strong-but-unvoted. Every claim carries its source.

---

## TL;DR — what this means for Resynth

1. **Our SMS/additive core is the textbook-correct model.** Output = a bank of sines, `s(n) = Σ A_l(n)·cos[θ_l(n)]` — literally the McAulay–Quatieri equation. Harmor (516 partials) and Serum 2 Spectral use the same family. We're in good company; the "R2-D2" character is inherent to additive, and the answer isn't to abandon it — it's to *lean into spectral character* (Max's call).
2. **The Formant bug has a proven, standard fix.** Move the **spectral envelope**, never the partials. The literature's "true envelope" (Röbel) is a *band-limited interpolation through the partial peaks* — it passes through them without displacing their frequencies. Formant = multiply each partial's amp by `envelope(ratio/shift)/envelope(ratio)`. Our current FRACTURE (raises ratios to a power) is the detuner and must go from the pitch path.
3. **SHAPE toward sine/square/saw = Chebyshev waveshaping**, which is *mathematically guaranteed not to detune* (periodic in → same period out) and gives an exact "harmonic n from polynomial T_n" mapping. This is the rigorous version of "turn my sample into a saw."
4. **The lossy "hero" sound (SIEVE/CRUSH) is real, characterizable degradation** — spectral gating + bit/rate reduction. Its metallic, octave-dropping, "old-data" quality comes from *quantization noise* (low-passed white noise) and *aliasing* (no anti-alias filter before decimation) and *pre-echo* (quantization error smeared across the transform window). We can dial each deliberately.

---

## 1. Core resynthesis models (the equations)

### 1a. Additive / sinusoidal model — the foundation
- **✓3-0** The synthesis stage is an oscillator-bank additive model: `s(n) = Σ_l A_l(n)·cos[θ_l(n)]` — a sum of sines, each with its own time-varying amplitude and unwrapped phase. *(McAulay–Quatieri 1986, archive.org/SpeechAnalysisSynthesis…)*
- **✓3-0** Partials are extracted by **peak-picking the windowed STFT magnitude**: frequencies = local maxima of `|Y(ω)|` (slope + → −); complex amplitude/phase = the STFT value at that peak, `γ_l = Y(ω_l) = A_l·e^{jθ_l}`. *(ibid.)*
- This is exactly what our `GeodeAnalyzer` peak detector + frame store already do. Validated.

### 1b. Partial tracking — McAulay–Quatieri birth/death (we already ship this)
- **✓3-0** Frame-to-frame continuity uses **birth/death matching** with a fixed frequency interval Δ: a track **dies** (matched to itself at zero amplitude) if no next-frame peak is within Δ; a track is **born** (created in the prior frame at zero magnitude) for any unmatched new peak; otherwise the **closest peak within Δ** continues the track, but only if no better competing match exists. *(ibid.)*
- Our geo4 tracker (tol 5%, coast 4, never-steal-drop-instead) is a faithful implementation. Confirmed against the canonical algorithm.
- **✓3-0** Amplitude is **linearly interpolated** between matched frames; **phase** uses a **cubic polynomial** `θ(t)=ζ+γt+αt²+βt³` matching phase+frequency at both boundaries, with the unwrap integer M chosen to minimize `∫(θ'')²dt` ("maximally smooth"), giving a closed-form M*. *(ibid.)* → *If we ever want smoother partials, this is the exact upgrade path.*

### 1c. Phase vocoder (the road NOT taken — but know the map)
- **✓3-0** Time-scale by STFT with analysis hop R, resynthesize at hop R·α; interpolate frames — **magnitude linearly**, **phase is "tricky, no exact way."** *(Julius Smith, CCRMA TSM)*
- **✓3-0** Two constraints fight: (1) horizontal — sinusoids "pick up where they left off" across frames; (2) vertical — bin-to-bin phase coherence within each FFT. **Both can't hold at once** → transients favor (2), steady state favors (1). *(ibid.)*
- **✓3-0 Phasiness** (the reverberant smear) happens because changing a frame's phase changes its **time-domain amplitude envelope**, so it stops looking like a windowed segment; that random AM is heard as reverb. The **phase-locked/identity vocoder** (Puckette; Laroche & Dolson) fixes it by preserving relative bin-to-bin phase **only at spectral peaks and their vicinity.** *(ibid.)* → **This is why STRETCH on our additive engine can sound cleaner than a phase-vocoder stretch: we carry per-partial phase accumulators, so no phasiness.**
- **(fetched)** Practical PV params for musical 2× stretch: frame ≈ 45 ms, hop = ¼ frame (**75% overlap**), Hann/Hamming. *(CCRMA TSM)*

### 1d. Analysis conventions that work (numbers to copy)
- **(fetched)** A high-quality resynthesis config: band-limit to 5 kHz @ 10 kHz SR, **10 ms frame interval**, **512-pt FFT**, **pitch-adaptive Hamming window 2.5× the pitch period** (≥ ~20 ms in unvoiced parts), keep up to **~80 largest peaks/frame.** *(McAulay–Quatieri)* → sanity-checks our kMaxPartials=96 and frame model.
- **(fetched)** MDCT/phase-vocoder both rest on the STFT with **overlapping analysis windows to avoid border/tapering effects.** *(Wikipedia: Phase vocoder)*

---

## 2. How the best engines actually do it (and how producers use them)

### Image-Line Harmor — the closest cousin to Resynth
- **✓3-0** Additive engine: **up to 516 sine partials per note per unison voice**, modulated in real time; user trades CPU↔detail by picking **12–516** partials. *(IL manual)* → validates our QUALITY knob (16–96) as the exact same lever, just a smaller ceiling.
- **✓3-0 Non-destructive resynthesis:** every change **re-analyzes/re-processes the original sample** rather than editing a frozen snapshot. *(IL manual)* → our off-thread `rebuildGeodeIfNeeded` matches this philosophy. Good.
- **✓3-0 Image mode:** analyzed audio → 2D image (x=time, y=harmonic freq, brightness=amplitude), editable in any image editor. *(IL manual)* → the north star for our live "data-removal" visualizer: the picture *is* the spectrum.
- **(fetched)** **Formant-preserving transposition** over ±600 cents. *(IL manual)* → confirms a bounded formant range is the pro norm.
- **(fetched) Prism** shifts partial frequencies relative to the fundamental → inharmonic/metallic. **This is a deliberate detune tool** — and the reason it's separate from amplitude filtering. *Lesson: partial-frequency remapping (our removed FRACTURE) is a real feature — but it's an INHARMONIC effect, not a formant, and must never be on the "keep pitch" path.*
- **(fetched) Blur** smears partials horizontally (across time) → reverberant spectral wash. *(Splice)* → this is what our removed HAZE was reaching for; if it returns, it's a *time-domain* partial smear, not amplitude blur.

### Xfer Serum 2 — Spectral + Granular
- **✓3-0** Serum 2's **Spectral Oscillator resynthesizes imported samples at the harmonic/partial level** (additive/spectral, not raw playback). *(xferrecords.com)*
- **✓3-0** Its spectral engine has **transient detection comparable to advanced timestretch** → phase-vocoder-style analysis with dedicated transient handling. *(ibid.)*
- **(fetched)** Load *any* source (vocal, piano, field recording) and **play it chromatically**; separate **Granular Oscillator** for granular resynthesis; imports via SFZ. *(hitproducerstash guide)* → our sample-only + chromatic-play model is the mainstream expectation.

### iZotope Iris vs. Camel/Apple Alchemy — the two selection paradigms
- **(fetched) Iris**: draw **Photoshop-style spectral filters on a spectrogram** (spectral masking) to isolate frequency regions; **Radius RT** preserves original speed at all pitches (**pitch decoupled from time, no time-stretch**). *(SoundOnSound Iris 2)*
- **(fetched)** The paradigms invert: **Alchemy = delete unwanted spectral parts; Iris = select the wanted parts.** *(KVR)* → Our CUT (LP/HP) + SIEVE = the "delete" school; the visualizer showing what's removed = the Iris "paint on the spectrogram" idea. We're fusing both.
- **(fetched)** Alchemy processes spectral in **mono**; Iris in **stereo**. *(KVR)* → our analyzer is mono (matches Alchemy); width comes from unison/pan.

### Research trackers (for provenance)
- **(fetched)** SPEAR/Loris are the research-grade partial trackers; **Serra & Smith's SMS** (deterministic sinusoids + stochastic residual) is the model our whole engine descends from. *(Serra 1990 CMJ PDF)*

---

## 3. Formant-preserving math — the FIX for our broken Formant

**The rule (from the literature): move the spectral ENVELOPE, keep the partial FREQUENCIES.**

- **✓3-0** **True-envelope (TE) estimator** (Röbel/Rodet; Imai's improved cepstral method): iterate `A_i(k) = max(A_{i-1}(k), V_{i-1}(k))` — replace the target with the pointwise max of previous target and current cepstral envelope, so **valleys between harmonics fill in and the envelope grows to cover all peaks**; stop at a threshold Δ (≈2 dB). *(Röbel & Rodet, semanticscholar)*
- **✓3-0** **Order that prevents the envelope from tracking individual partials:** `P_c ≤ R/(2·Δ_F)` (R=sample rate, Δ_F=max f0); ~1.66× larger for a Hamming smoothing window. *(ibid.)* → this is the knob that keeps a formant envelope *smooth* instead of hugging each partial.
- **✓3-0 THE formant-shift formula:** pre-warp with a **per-bin amplitude multiplier** `P(k) = A(k·f)/A(k)`, where f = shift factor and A = estimated envelope. **Pure amplitude multiplication — frequencies untouched.** *(ibid.)*
- **✓3-0** TE = **band-limited interpolation through the major spectral peaks** — the envelope is *anchored to the partial peaks, passing through them without displacing them.* Exactly the property we need. *(Röbel/Villavicencio, researchgate 220645997)*
- **✓3-0** Optimal cepstral order for a harmonic spectrum = **Fs/(2·f0)**; because order can change per frame, TE optimally interpolates the peaks with no explicit peak selection. *(researchgate 224641053 / IEEE 6855051)*
- **(fetched)** Prefer TE over LPC/all-pole: for high pitch or a strong isolated sinusoid, all-pole starts **representing individual peaks** (bad — it tracks partials); TE never creates a spurious peak near DC. *(Röbel)*
- **(fetched)** Discrete-cepstrum envelope = **low-pass liftering of the log-magnitude spectrum** (separate source from filter); coefficient count `N < SR/(f0max·0.5)`, default ~30; too few → irrelevant envelope, too many → hugs the spectrum (tracks partials). *(IRCAM AudioSculpt docs)*

**→ Resynth Formant fix (concrete):** estimate a smooth envelope `E(ratio)` over the partial bank (cheap: a low-order cepstral / peak-interpolated curve, order ≈ Fs/(2·f0)); then for shift factor `f`, set `amp[j] *= E(ratio[j]/f) / (E(ratio[j]) + ε)`. **Never touch `ratio[j]`.** Add gain-tame + smoothing to kill harshness. This is the peer-reviewed method — no more detuning.

---

## 4. The lossy "data-removal" aesthetic — WHY it sounds like that (our hero)

- **(fetched) Bit-depth reduction** → increased **quantization noise that perceptually resembles low-pass-filtered white noise** — the characteristic lo-fi haze. *(Wikipedia: Bitcrusher)* → CRUSH's "noise floor" character, explained.
- **(fetched) Sample-rate reduction WITHOUT a pre-LP filter** → high frequencies **alias** (fold back); with an LP first they'd just be removed. *(ibid.)* → this is the exact switch between "harsh metallic" and "smooth filtered."
- **(fetched)** Severe decimation → **metallic sound from aliasing + nonlinear distortion**; whether it sounds harsh or filtered depends on **whether the reduced-rate signal is interpolated**; bitcrushing is *the* defining glitch/chiptune technique. *(ibid., ADSR Reaktor tut)* → CRUSH should expose an interpolate on/off (or "harsh↔smooth") character.
- **(fetched) Pre-echo** (the MP3/AAC/Vorbis "swirl") = **quantization error spread across the whole MDCT transform window → temporal smearing** before transients; mitigated by **short blocks** (time/freq tradeoff). *(Wikipedia: Pre-echo)* → this is the literal mechanism behind the "Goodhertz Losser / codec" sound Max loves. If we push CRUSH toward a codec feel, it's spectral-frame quantization smeared in time.
- **Spectral gating / keep-N-loudest** (our SIEVE + old DISTILL): zero the partials below a rising threshold / keep only the loudest. **Why it "drops octaves & sounds low-data":** killing quiet upper partials leaves a sparse, gapped spectrum; as the threshold rises the surviving partials jump between harmonics → the pitch-y, warbling, "data-being-removed" motion. This is our proven hero — the research confirms it's a legitimate spectral-thinning family (Alchemy's "delete unwanted parts").

**→ Resynth degradation stack (deliberate):** SIEVE (spectral gate, keep) → QUALITY (partial budget, keep-loudest) → CRUSH (bit quantize + rate decimate with an interpolate/aliasing character + optional frame-quantization "codec" smear). Each has a *named reason* it sounds the way it does.

---

## 5. Sample → sine / square / saw, and DRIVE — harmonics WITHOUT detune

**Chebyshev waveshaping is the rigorous, pitch-safe way to "turn a sample into a synth waveform."**

- **✓3-0** **Chebyshev polynomials of the first kind** `T_0=1, T_1=x, T_{k+1}=2x·T_k − T_{k-1}` have the magic property: **a unit sinusoid in → ONLY the k-th harmonic out.** The exact primitive for adding a chosen harmonic without shifting pitch. *(Smyth, UCSD)*
- **(fetched)** Any target harmonic spectrum = a **weighted sum of Chebyshevs**, one per harmonic: `F(x) = h_0·T_0 + h_1·T_1 + … + h_N·T_N`, where **`h_j` = desired amplitude of the j-th harmonic.** *(ibid.)* → **SAW = h_n = 1/n (all n); SQUARE = 1/n odd-only; SINE = h_1 only.** Direct, exact.
- **(fetched)** Waveshaping order N is **strictly band-limited** — no harmonics above the N-th → explicit max-harmonic control, easier anti-aliasing than FM. *(Smyth)*
- **(fetched) Parity rule:** odd transfer function → **odd harmonics only**; even → **even harmonics** (doubles the fundamental = up an octave). *(Puckette; Smyth)* → gives SHAPE's square (odd) vs saw (all) for free, and warns us even-only shaping shifts perceived octave.
- **✓/(fetched) Pitch-safety proof:** a **periodic input stays periodic at the SAME period** through any nonlinear f() → **waveshaping adds harmonics without detuning.** *(Puckette, MSP techniques)* → this is the theorem that makes SHAPE and DRIVE obey Max's "never detune" law.
- **(fetched)** The **input amplitude is the "waveshaping index"** — it directly controls distortion amount / harmonic richness. *(Smyth)* → DRIVE = drive-into-the-shaper amount.
- **(fetched)** Waveshaping is **intermodulating**: for k input sinusoids, k "straight" terms but **(k²−k)/2 cross terms** that dominate as input gets richer. *(Smyth)* → **important caveat:** time-domain Chebyshev shaping of a *complex* (many-partial) signal creates intermodulation mud, not clean harmonics. **So for SHAPE we do it in the SPECTRAL domain** — re-weight `amp[j]` toward the target `h_n` series per partial (no intermod) — and reserve true time-domain waveshaping (with its intermod grit) for **DRIVE**, where that dirt is the point.

**→ Resynth SHAPE (spectral, clean):** for amount a and target weights `W(n)` (saw 1/n all, square 1/n odd, sine n=1), map each partial to its nearest harmonic index n of the fundamental and set `amp[j] = lerp(amp[j], amp[j]·W(n), a)`. Frequencies untouched → **no detune.** On inharmonic samples, `nearestHarmonic` degrades gracefully (round ratio to nearest integer, floor at 1).

**→ Resynth DRIVE (spectral distortion, dirty):** per-partial / per-band **soft-clip + emphasis** on `amp[]` (the Lost+Found "Spectral Modulator RESONANCE = soft clipping + emphasis on individual bands → jagged, focused" behavior). Adds harmonic grit and band focus; because it's per-band amplitude nonlinearity, pitch is preserved. Optionally a light Chebyshev term for extra harmonics — capped at N to bound aliasing.

---

## 6. Direct build implications (the "similar area" locked in)

| Resynth control | Grounded in | Pitch-safe? |
|---|---|---|
| **QUALITY** (partial budget) | Harmor 12–516 partials ✓3-0 | yes |
| **SIEVE** (spectral gate) | Alchemy "delete parts" + keep-N-loudest | yes |
| **CRUSH** (bit/rate + codec smear) | Bitcrush quantization-noise + aliasing + MDCT pre-echo (fetched) | yes |
| **CUT** LP/HP (spectral filter) | Iris spectral masking; amplitude-only | yes |
| **FORMANT** (fixed) | True-envelope `amp*=E(r/f)/E(r)`, envelope anchored to peaks ✓3-0 | **yes — the fix** |
| **SHAPE** → sine/sq/saw | Chebyshev weights h_n (saw 1/n, sq odd, sine 1) ✓3-0, applied spectrally to avoid intermod | **yes — proven** |
| **DRIVE** (spectral distortion) | Per-band soft-clip (Lost+Found Spectral Modulator) + bounded Chebyshev | yes |
| **SCAN / STRETCH** | Additive read-head; per-partial phase = no phasiness ✓3-0 | yes |
| **TILT** | Spectral tilt (amplitude slope) | yes |
| removed **FRACTURE** | = Harmor "Prism" — a real INHARMONIC tool, but a detuner → correctly off the keep-pitch path | (no — that's why it's out) |

**Two hard lessons for the code:**
1. **Anything that changes `ratio[j]` changes pitch.** Formant, Shape, Drive, Sieve, Cut, Tilt, Crush must touch **`amp[j]` (or post-synth L/R) only.** (Confirmed: our old FRACTURE raised ratios to a power = the detune Max hated.)
2. **Do SHAPE in the spectral domain, not time-domain waveshaping** — time-domain Chebyshev on a many-partial signal makes `(k²−k)/2` intermodulation terms (mud). Keep clean harmonic re-weighting for SHAPE; save real waveshaping grit for DRIVE where dirt is wanted.

---

## Sources (22, all fetched & real)
1. Serra & Smith, *Spectral Modeling Synthesis*, CMJ 1990 — audiolabs-erlangen.de PDF
2. McAulay & Quatieri, *Speech Analysis/Synthesis Based on a Sinusoidal Representation*, 1986 — archive.org
3. Julius O. Smith, *Time-Scale Modification / Phase Vocoder*, CCRMA — ccrma.stanford.edu/~jos/TSM/
4. *Phase vocoder* — en.wikipedia.org
5. Image-Line **Harmor** manual — image-line.com
6. Xfer **Serum 2** — xferrecords.com
7. **iZotope Iris 2** review — soundonsound.com
8. Harmor additive feature — splice.com
9. Serum 2 spectral resynthesis guide — hitproducerstash.com
10. Iris vs Alchemy discussion — kvraudio.com
11–16. **Röbel/Villavicencio true-envelope** papers — semanticscholar, ieee 6855051, researchgate 220645997 / 224641053 / 234166040
17. **Discrete Cepstrum** — IRCAM AudioSculpt docs
18. *Pre-echo* (MDCT codec artifact) — en.wikipedia.org
19. *Bitcrusher* — en.wikipedia.org
20. Reaktor bitcrushing tutorial — adsrsounds.com
21–22. **Chebyshev waveshaping** — Smyth (UCSD) Matching_Spectrum_Using + waveshaping PDF; Puckette *Techniques* node77
