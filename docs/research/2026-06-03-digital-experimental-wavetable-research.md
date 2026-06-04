# Digital + Experimental Wavetable Research — Phase 11l

**Date:** 2026-06-03
**Author:** Research subagent
**Purpose:** Concrete harmonic specs for 8 Digital + Experimental wavetables that sound authentically like their names and are distinct from each other and from the Analog/Vocal/Metallic/Morph categories.

---

## Sources cited

### Web research
- [PPG Wave — Wikipedia](https://en.wikipedia.org/wiki/PPG_Wave)
- [PPG Wave 2.3 & Waveterm B — Sound On Sound](https://www.soundonsound.com/reviews/ppg-wave-23-waveterm-b)
- [PPG Wavetable List — PresetPatch](https://www.presetpatch.com/articles/ppg-wavetable-list)
- [PPG Wave 2.2 / Wave 2.3 simulator — Hermann Seib](https://www.hermannseib.com/english/synths/ppg/wavesim.htm)
- [PPG Wave 2.3 docs (ppg.synth.net)](https://ppg.synth.net/wave22/)
- [DX7 Technical Analysis — ajxs.me](https://ajxs.me/blog/Yamaha_DX7_Technical_Analysis.html)
- [DX7 Algorithm reverse-engineering — righto.com](http://www.righto.com/2021/12/yamaha-dx7-chip-reverse-engineering.html)
- [FM Electric Piano tutorial — Attack Magazine](https://www.attackmagazine.com/technique/tutorials/fm-electric-piano/)
- [Roland D-50 — Wikipedia](https://en.wikipedia.org/wiki/Roland_D-50)
- [Linear Arithmetic synthesis — Wikipedia](https://en.wikipedia.org/wiki/Linear_Arithmetic_synthesis)
- [Roland D-50 — Sound On Sound](https://www.soundonsound.com/reviews/roland-d50)
- [Korg M1 Piano 16 Explained — ProducerStack](https://producerstack.com/blogs/the-stack/the-m1-piano-16-the-sound-that-built-house-music)
- [Korg M1 Retrozone — Sound On Sound](https://www.soundonsound.com/reviews/korg-m1-retrozone)
- [Historical Recording Characteristics (78rpm/30s era) — Pspatial Audio](https://pspatialaudio.com/record_characters.htm)
- [78rpm Restorations — filtering/bandwidth](http://www.78rpmrestorations.com/techniques/filtering.html)
- [Serum Wavetable Synthesis — EDMProd](https://www.edmprod.com/wavetable-synthesis/)
- [Outerverse.fm — 8 Ways To Make Wavetables](https://outerverse.fm/blogs/tutorials/blog-8-ways-to-make-wavetables/)

### Vital source code (reference for spectral patterns and implementation precedents)
- `/Users/macshooter/Downloads/vital-main/src/common/wavetable/phase_modifier.h` + `.cpp` — PhaseStyle enum: kNormal, kEvenOdd, kHarmonic, kClear. kClear zeros all phases (produces "static" version of any WT).
- `/Users/macshooter/Downloads/vital-main/src/common/wavetable/frequency_filter_modifier.h` + `.cpp` — LP/HP/BP/Comb spectral envelope sweep.
- `/Users/macshooter/Downloads/vital-main/src/common/wavetable/wave_source.cpp` — `linearFrequencyInterpolate`: interpolates amplitude AND phase per-bin.
- `/Users/macshooter/Downloads/vital-main/src/common/wavetable/shepard_tone_source.h` — harmonic octave-shift technique.

### Existing Terrain wavetable implementations (in Wavetable.h)
- `makePPGWave()` — existing legacy time-domain version (line 813). Uses Gaussian harmonic peak that shifts from h≈1 to h≈6 across frames, plus 4-bit quantization (`round(s*8)/8`). Uses `Wavetable(16)` legacy constructor.
- `makeDX7EP()` — existing legacy version (line 845). Uses 1+0.5×h2 + bellAmp×0.6×h3.5 + bellAmp×0.3×h7. Uses legacy constructor.
- `makeD50Bell()` — existing legacy version (line 867). Uses inharmonic partials {1.0, 2.756, 5.404, 8.93}. Uses legacy constructor.
- `makeM1Piano()` — existing legacy version (line 889). Uses h1+0.5h2+0.33h3+0.2×h(4+detuneAmt)+0.15h5+0.1h6. Uses legacy constructor.
- `makeDustbowl()` — existing (line 1118). Saw + high-passed noise 0→40%. Uses legacy constructor.
- `makeStaticEvolve()` — existing (line 1154). Pure noise frame 0 → saw+noise blend frame 15. Uses legacy constructor.
- `makeSpectralDrift()` — existing (line 1184). Per-frame randomized harmonic phases (fixed per-frame seed). Uses legacy constructor.
- `makeSerumHD()` — existing (line 1213). Gaussian spectral envelope center 2→10, 15% even boost. Uses legacy constructor.

---

## Critical observation: existing implementations need rework

After reading all 8 existing implementations in `Wavetable.h`, I found that **they are all functional and reasonable approximations but have three systematic weaknesses**:

1. **Not dramatic enough on WT POS** — like the analog/morph categories pre-Phase-11j, the digital/experimental tables often have the same spectrum in every frame (or only gentle changes). Per the Phase 11h research (which is in `2026-06-03-wavetable-bank-expansion-research.md`), every frame must be "more of something" compared to the last.

2. **Digital category doesn't sound distinctly digital** — the existing PPGWave uses a Gaussian-envelope-shifted saw with mild quantization. The existing DX7EP is a fixed partial mix. Neither uses the mip-enabled `buildFromSpec()` path, so they lose anti-aliasing.

3. **Experimental category is conceptually right but tonally weak** — StaticEvolve and Dustbowl work well; SpectralDrift and SerumHD need more frame-to-frame drama.

The Phase 11l task is to rewrite these as `WavetableSpec`-based implementations using `buildFromSpec()` for anti-aliasing, with significantly more dramatic frame-to-frame variation, and authentic sonic identities rooted in the source instruments.

---

## What makes digital sound distinctly digital (vs. analog)

Analog category (ProphetSaw, OBXSaw, JunoStr) is characterized by:
- Continuous harmonic ladders (h1..hN at smooth 1/h rolloffs)
- Phase scatter that mimics VCO beating
- Warm spectral tilt (more energy in low harmonics)

Digital category must sound DIFFERENT by having:
- **Quantization artifacts** — harmonic amplitudes that are stepped, not smooth (PPGWave was 8-bit, 256 levels; DX7 used 12-bit operators)
- **Inharmonic partials** — non-integer frequency ratios from FM sidebands (DX7) or LA transient PCM (D-50)
- **Abrupt spectral transitions** — the PPG's 8-bit waveform table contains waveforms with steeply bandlimited spectra that jump rather than morph smoothly
- **Specific harmonic clusters** — FM sidebands at fm±n×fc create energy at specific non-adjacent harmonics rather than a continuous ladder
- **Missing harmonics** — 8-bit sine-summed waveforms from the PPG bank often have ZERO energy at many harmonics because the bank waveforms are discrete and algorithmically generated

---

## Digital Category

---

### 1. PPGWave

**Sonic identity:**

The PPG Wave 2.2/2.3 (Wolfgang Palm, 1981–84) sounds unmistakably "digital cold" — hard, glassy, slightly metallic, with an icy top end when upper wavetables are selected. This comes from three things: (a) 8-bit DAC quantization creating audible quantization noise, (b) the waveform bank structure where each waveform in a table is a fundamentally different harmonic recipe (not a smooth interpolation), and (c) the tendency of upper-bank waveforms to have energy concentrated in a narrow high-harmonic cluster — which is the "ice" or "zing" character. The PPG has 30 wavetables × 64 waves per table. Individual waveforms in each table range from sine-like (lower waves) to increasingly complex (upper waves), with the harmonic energy climbing from low to high registers. Wavetable 00 specifically has harmonics 1–8 very strong at wave 0 (resonant filter simulation), then as wave position increases the peak migrates upward.

**Harmonic signature:**

- The defining PPG character is a harmonic envelope that is NOT smooth 1/h but rather has a **prominent Gaussian peak at a specific harmonic** — all other harmonics fall off sharply. This creates a "hollow tube" or "singing pipe" quality that changes dramatically as WT POS (wave position) changes.
- At low frames: energy concentrated near h1-h3 (warm, hollow, rounded)
- At mid frames: energy peak migrates to h4-h8 (nasal, midrange ring, like blowing air through a metal tube)
- At high frames: energy peak migrates to h10-h18 (icy, glassy, the distinctive PPG "digital cold" — very little fundamental, lots of high-harmonic sizzle)
- The 8-bit quantization (`round(x * 128) / 128.0` for true 8-bit signed, or `round(x * 8) / 8.0` for the 4-bit simulation currently in Wavetable.h) creates quantization "grit" — this is best approximated in the additive domain by adding a small amount of white harmonic noise at high-frequency bins (harmonics 64-128 with small equal amplitudes).

**Why existing implementation needs rework:**

The existing `makePPGWave()` (line 813) applies a Gaussian envelope with center migrating from h≈1 to h≈6 — range is too narrow (should go to h≈18 or higher for the "icy" character). Quantization at `round(s*8)/8` (effectively 4-bit) is done in the time domain after synthesis, which is correct but loses the per-frame dramatic difference. The migrating Gaussian needs wider range and should be implemented via `buildFromSpec()` for mip-based anti-aliasing.

**WT POS sweep design:**

- Frame 0: Gaussian peak centered at h=1.5, sigma=1.0 → mostly fundamental (warm, sine-like)
- Frame 3: Gaussian centered at h=3, sigma=1.5 → hollow nasal tube
- Frame 6: Gaussian centered at h=5, sigma=2.0 → midrange pipe organ character  
- Frame 9: Gaussian centered at h=9, sigma=2.5 → upper-mid zing, starting to go "digital"
- Frame 12: Gaussian centered at h=14, sigma=3.0 → high-harmonic cluster, classic PPG "icy"
- Frame 15: Gaussian centered at h=20, sigma=3.5 → extreme high-harmonic peak, almost all fundamental gone, pure icy digital brilliance + quantization grit noise floor (harmonics 48-80 with small equal amplitude 0.03)

**Mathematical spec:**

```
For frame f (0..15), t = f/15.0:
  center(f) = 1.5 + 18.5 * t           // 1.5 → 20.0 (harmonic center migrates high)
  sigma(f)  = 1.0 + 2.5  * t           // 1.0 → 3.5  (gets slightly wider as it rises)
  For h = 1..80:
    A[h] = exp(-(h - center)^2 / (2*sigma^2))
  // Quantization grit: add flat noise floor at h=50..80 simulating 8-bit DAC
  gritAmp(f) = t * 0.04                 // 0 at frame 0, 0.04 at frame 15
  For h = 50..80: A[h] += gritAmp(f)   // adds digital noise floor
  numHarmonics = 80
```

Note: The quantization grit (flat high-harmonic noise) is the key differentiator from the analog category. Analog saws have 1/h rolloff — PPGWave has the opposite: an almost-silent fundamental with a cluster of energy at a specific high-harmonic range.

**Implementation spec:**

```cpp
static WavetableSpec makePPGWaveSpec()
{
    WavetableSpec spec;
    constexpr int kGritStart = 50;
    constexpr int kGritEnd   = 80;
    for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
    {
        const float t      = (float) f / 15.0f;
        const float center = 1.5f + 18.5f * t;    // h peak: 1.5 → 20.0
        const float sigma  = 1.0f + 2.5f  * t;    // bandwidth: 1.0 → 3.5
        const float grit   = t * 0.04f;            // 8-bit noise floor: 0 → 0.04

        FrameSpec& fs = spec.frames[(size_t) f];
        fs.numHarmonics = kGritEnd;

        for (int h = 1; h <= kGritEnd; ++h)
        {
            const float d  = (float) h - center;
            float amp = std::exp (-(d * d) / (2.0f * sigma * sigma));
            // Add grit at high harmonics (simulates 8-bit DAC quantization floor)
            if (h >= kGritStart)
                amp += grit;
            fs.amplitudes[(size_t)(h - 1)] = amp;
            fs.phases[(size_t)(h - 1)]     = 0.0f;
        }
    }
    return spec;
}
```

**Sonic character test:** At frame 0 it should sound almost like a sine. At frame 7-8 it should have a distinctive hollow, nasal "pipe organ" quality. At frame 15 it should sound icy and metallic — almost all fundamental gone, top-end energy dominant, slightly grainy from the grit floor. This is the unmistakable PPG Wave sound.

---

### 2. DX7EP (FM electric piano)

**Sonic identity:**

The Yamaha DX7 "E PIANO 1" preset is one of the most recognized sounds in pop history. It sounds: glassy, bell-like attack with a warm piano body, the attack has a characteristic metallic "tine" quality (like a struck metal rod), and it decays fast with a bright upper-harmonic shimmer. The key FM parameters for this sound are:
- Algorithm 5 (from web research): three independent 2-operator stacks
- Carrier at ratio 1.0 (the fundamental)
- First modulator at ratio 14:1 (creates bell partials at 1 ± 14n sidebands)
- Modulation index starting high (bright tine attack) and decaying fast (the FM envelope causes the modulation to fade, leaving a pure tone)

**FM sideband math:**

For a carrier at frequency fc with a modulator at fm = 14×fc and index β:
Sidebands appear at fc ± n×fm = fc × (1 ± 14n)
- n=0: h=1 (fundamental)
- n=1: h=15, h=-13 (reflected as h=13)
- n=2: h=29, h=-27 (reflected as h=27)

With a 3.5:1 modulator (the "tine" quality), sidebands at:
- n=0: h=1
- n=1: h=4.5 (inharmonic — this is the key: it's BETWEEN harmonics 4 and 5)
- n=2: h=8 (harmonic)
- n=3: h=11.5 (inharmonic again)

The coexistence of harmonic and inharmonic sidebands (3.5×, 7×, 10.5×, 14×) gives the DX7 EP its bell-like but also slightly "wrong" quality — neither clean piano nor pure bell. The FM index controls the amplitude of the sidebands; high index = bright tine attack; low index = warm sustained tone.

**Why existing implementation needs rework:**

The existing `makeDX7EP()` (line 845) uses partials at 1, 2, 3.5, 7. These are correct but the frame sweep just grows bellAmp from 0→1, meaning frame 0 is a pure tone and frame 15 is the fullest "bell ring." That's a reasonable sweep but misses the authentic FM character: the modulation INDEX should control how many sidebands are active. At high modulation index, the sideband energy spreads much further (to higher-order sidebands). The new spec should use proper Bessel-function-weighted sideband amplitudes.

**FM sideband amplitudes via Bessel functions:**

For FM with index β, sideband n has amplitude J_n(β) (Bessel function of first kind).
For β=0: J_0=1, J_n≈0 for n>0 (pure carrier = pure tone)
For β=1: J_0=0.77, J_1=0.44, J_2=0.11 (3 sidebands)
For β=2: J_0=0.22, J_1=0.58, J_2=0.35, J_3=0.13 (4 sidebands, fundamental REDUCED)
For β=3: J_0=-0.26, J_1=0.34, J_2=0.49, J_3=0.31, J_4=0.13 (fundamental goes negative)
For β=4: J_0=-0.40, J_1=-0.07, J_2=0.36, J_3=0.43, J_4=0.28 (dramatic redistribution)

This means: as modulation index decreases (frame 0 = high β, frame 15 = low β), the sidebands collapse back to just the fundamental — which is exactly the DX7 EP attack → sustain envelope behavior. We reverse this for WT POS: frame 0 = low β (warm sustain), frame 15 = high β (bright tine attack).

**WT POS sweep design:**

- Frame 0: β≈0.5 → almost pure tone (h=1 dominant, h=3.5 trace) — the warm "sustain" of the EP
- Frame 3: β≈1.0 → moderate tine ring, h=1 and h=3.5 approximately equal
- Frame 7: β≈2.0 → J_0=0.22, J_1=0.58 — fundamental receding, tine sideband dominant
- Frame 11: β≈3.0 → complex redistribution, multiple sidebands active
- Frame 15: β≈4.5 → very high index, extreme bell attack character, many sidebands

**Mathematical spec:**

Modulator ratio = 3.5 (the classic EP tine ratio). Sidebands appear at: h = 1 + n×3.5 and h = 1 - n×3.5.
For our additive implementation, fractional harmonics (e.g., 3.5, 7.0, 10.5) are treated as partials at those specific ratios via `sin(h * 2π * normPhase)` — this works because `buildFromSpec()` uses the harmonic index directly as an integer h, so we can only hit integer harmonics. For DX7EP we must use the legacy time-domain constructor to correctly place the 3.5× partial.

```
For frame f, t = f/15.0:
  β(f) = 0.5 + 4.0 * t           // modulation index: 0.5 → 4.5
  modRatio = 3.5                   // the EP tine ratio
  
  Time-domain synthesis (legacy constructor):
  For each sample i:
    phase    = 2π * i / N
    modPhase = β(f) * sin(modRatio * phase)    // FM modulation
    y        = sin(phase + modPhase)            // FM carrier output
    // Add harmonic envelope: weight toward fundamental × harmonic decay
    + 0.3 * sin(2 * phase)                      // 2nd harmonic body
    + 0.15 * sin(3 * phase)                     // 3rd harmonic warmth
```

**Implementation spec (legacy time-domain for non-integer partials):**

```cpp
static Wavetable makeDX7EPSpec()
{
    Wavetable wt (16);
    const double twoPi = 2.0 * 3.14159265358979323846;
    const int N = wt.frameSize_;
    for (int f = 0; f < 16; ++f)
    {
        const double t          = (double) f / 15.0;
        const double beta       = 0.5 + 4.0 * t;      // FM index: 0.5 → 4.5
        const double modRatio   = 3.5;                  // EP tine: inharmonic partial
        const double beta2      = t * 1.5;             // secondary modulator for h7
        const double mod2Ratio  = 7.0;

        for (int i = 0; i < N; ++i)
        {
            const double phase = twoPi * (double) i / (double) N;
            // Primary FM voice: carrier at h=1 modulated by h=3.5
            const double mod1    = beta  * std::sin (modRatio  * phase);
            const double mod2    = beta2 * std::sin (mod2Ratio * phase);
            const double carrier = std::sin (phase + mod1 + mod2);
            // Add piano body harmonics (not FM, just additive coloring)
            const double body    = 0.25 * std::sin (2.0 * phase)
                                 + 0.12 * std::sin (3.0 * phase);
            wt.sampleRef (f, i)  = (float)((carrier + body) * 0.45);
        }
    }
    return wt;
}
```

**Sonic character test:** Frame 0 should sound like a warm electric piano in its sustain phase — fundamental dominant, slight warmth. Frame 7-8 should sound like the struck tine with the characteristic bell-like "ting" quality — lots of upper harmonic energy at odd-harmonic-plus-half positions. Frame 15 should sound like a struck metal tine at maximum brightness — almost bell-like, very complex, the bright "clang" of the EP attack moment. The key identifier: it must NOT sound like a regular harmonic sawtooth or a clean sine. The 3.5× modulator creates partials that fall BETWEEN integer harmonics, giving the characteristic FM "metallic" quality.

---

### 3. D50Bell (Roland D-50 LA synthesis bell)

**Sonic identity:**

The Roland D-50 (1987) uses Linear Arithmetic (LA) synthesis: short PCM attack samples + digital synthesis body. The characteristic D-50 bell sound ("Fantasia," "Digital Native Dance," and numerous bell patches) has: (a) a sharp inharmonic transient from a PCM bell sample, (b) a ring-down that follows the specific partial frequencies of a real bell or tubular bell, and (c) the LA synthesis body filling in a sustained synthetic waveform underneath. The key acoustic physics: real bells vibrate in a specific set of inharmonic modes. For a tubular bell, the primary partials are approximately:

- h × 2.756 (the "hum" tone partial of cylindrical bells)
- h × 5.404 (the "tierce" or minor third partial)
- h × 8.933 (the "quint" or fifth partial)
- h × 13.02 (higher partial)
- h × 1.0 (fundamental, usually weaker in bells)

The existing Wavetable.h has `{1.0, 2.756, 5.404, 8.93}` which is accurate for tubular bell physics. This matches acoustic measurements of tubular bells documented in musical acoustics literature (these are the Chladni figures of a cylindrical tube).

**Harmonic signature:**

- No integer harmonics at all above h=1 (this is the key difference from ALL analog waveforms)
- Four inharmonic partials with ratios 1.0 : 2.756 : 5.404 : 8.93 : 13.02
- The WT POS sweep should change the DECAY character — as if listening to the bell at different time points after the strike:
  - Frame 0: all partials present at equal amplitude (the moment of the strike — dense, complex)
  - Frame 7-8: fundamental softens, upper partials sustain (mid-decay character)
  - Frame 15: fundamental and 2nd partial mostly gone, only the upper two partials remain (the long-ringing "shimmer" tail of the bell)

**Why existing implementation needs rework:**

The existing `makeD50Bell()` (line 867) goes the OPPOSITE direction — frame 0 is mostly fundamental, frame 15 adds the inharmonic partials. Acoustically, this is backwards: real bell strikes START with ALL partials at high amplitude, then the higher partials ring longer while the fundamental decays fastest. The correct sweep is: all partials equal at frame 0 → upper-partial emphasis at frame 15.

**WT POS sweep design:**

- Frame 0: all 5 partials at amplitudes [1.0, 0.8, 0.6, 0.4, 0.25] (strike — dense bell)
- Frame 4: decay begins; fundamental 0.7, h×2.756=0.8, h×5.4=0.65, h×8.93=0.5, h×13=0.35
- Frame 8: fundamental 0.3, h×2.756=0.7, h×5.4=0.65, h×8.93=0.55, h×13=0.45
- Frame 12: fundamental 0.1, h×2.756=0.5, h×5.4=0.6, h×8.93=0.55, h×13=0.5
- Frame 15: fundamental ≈0, h×2.756=0.3, h×5.4=0.7, h×8.93=0.7, h×13=0.6 (ringing shimmer — upper partials dominant)

**Mathematical spec:**

```
Bell partials (inharmonic ratios — these are NOT integer harmonics):
  r[0] = 1.0
  r[1] = 2.756
  r[2] = 5.404
  r[3] = 8.933
  r[4] = 13.02

For frame f, t = f/15.0:
  amp[0] = 1.0  * (1.0 - 0.95*t)    // fundamental decays nearly to zero
  amp[1] = 0.8  * (1.0 - 0.65*t)    // 2nd partial decays ~65%
  amp[2] = 0.6  * (1.0 - 0.15*t)    // 3rd partial barely decays
  amp[3] = 0.4  * (1.0 + 0.50*t)    // 4th partial GROWS slightly (bell physics: sustained)
  amp[4] = 0.25 * (1.0 + 1.00*t)    // 5th partial grows (long-ringing shimmer)

Time-domain synthesis (legacy constructor — inharmonic partials require this):
  y = Σ amp[p] * sin(r[p] * phase)  for p=0..4
```

**Implementation spec:**

```cpp
static Wavetable makeD50BellSpec()
{
    Wavetable wt (16);
    const double twoPi = 2.0 * 3.14159265358979323846;
    const int N = wt.frameSize_;
    // Tubular bell inharmonic partial ratios (from musical acoustics, Chladni modes)
    static const double r[] = { 1.0, 2.756, 5.404, 8.933, 13.02 };
    for (int f = 0; f < 16; ++f)
    {
        const double t = (double) f / 15.0;
        // Acoustic bell decay model: fundamental fastest, upper partials slowest
        const double amp[5] = {
            1.00 * (1.0 - 0.95 * t),          // h×1.0   : decays to near zero
            0.80 * (1.0 - 0.65 * t),          // h×2.756 : decays to 28%
            0.60 * (1.0 - 0.15 * t),          // h×5.404 : barely decays
            0.40 * (1.0 + 0.50 * t),          // h×8.933 : grows (sustained shimmer)
            0.25 * (1.0 + 1.00 * t),          // h×13.02 : grows (long ring)
        };
        for (int i = 0; i < N; ++i)
        {
            const double phase = twoPi * (double) i / (double) N;
            double s = 0.0;
            for (int p = 0; p < 5; ++p)
                s += amp[p] * std::sin (r[p] * phase);
            wt.sampleRef (f, i) = (float)(s * 0.35);
        }
    }
    return wt;
}
```

**Sonic character test:** Frame 0 should sound like the moment of striking a bell — dense, complex, lots of inharmonic content simultaneously. Frame 7 should be bell mid-decay — the fundamental has softened but the ring is present. Frame 15 should be the long shimmer tail — the upper harmonics ring on while the fundamental is nearly silent, producing an airy, sustained metallic shimmer. This is distinct from the Metallic category (BowedMetal, GlassHarmonics, Railroad) because it follows the specific bell acoustic decay model rather than a static partial mix.

---

### 4. M1Piano (Korg M1 piano)

**Sonic identity:**

The Korg M1 Piano (1988) is "the most-used piano sound in pop music history" (in productions from 1988-1998). It sounds: bright, transient-forward, extremely clear attack with a percussive "clank," mid-range presence that cuts through mixes, and a relatively fast decay with limited low-end warmth. Unlike a real acoustic piano it is:
- Brighter than real at attack (the 4MB PCM ROM was limited and Korg boosted presence)
- Has a characteristic mid-upper harmonic emphasis (the "honk") around h=4-6
- Has a slight "digitally perfect" clarity that no acoustic instrument has
- 16-bit PCM: no quantization noise, but with a characteristic harmonic "noise artifact" approximately halfway through the loop point (mentioned in Sound On Sound Retrozone review) that became part of the character

**Harmonic signature:**

Real piano acoustic physics: at the moment of hammer strike, all harmonics are present at high amplitude. Then the fundamental sustains longest, while high harmonics decay fastest. The M1 Piano approximates this with:
- Strong fundamental (h=1): 1.0 amplitude
- Strong 2nd harmonic (h=2): ~0.55 amplitude (brighter than real piano)
- Strong 3rd (h=3): ~0.38 (M1 has a bright, present 3rd that gives it "weight")
- 4th harmonic (h=4): ~0.28 with slight inharmonicity (piano strings are not ideal — `4.0 + ε` where ε≈0.01-0.04 per octave per harmonic, the "stiffness parameter")
- 5th-8th harmonics at ~0.2-0.08 with increasing inharmonicity

The M1 Piano's WT POS sweep should model the piano's dynamic character — how the sound changes with velocity/attack:
- Frame 0: soft voicing — mostly fundamental and 2nd harmonic, gentle
- Frame 7: medium — h1-h5 balanced, characteristic M1 "presence bump" in h3-h4 range
- Frame 15: hard strike — all harmonics up to h=10 present, strong upper-mid "clank," attack character

**Mathematical spec:**

```
Inharmonicity of piano strings: harmonic h sits at f × h × sqrt(1 + B×h²)
where B ≈ 0.00015 for a midrange piano string (middle C range).
This shifts h=4 from 4.0 to ~4.00 + 4²×0.00015×4 ≈ 4.010 (very small but audible)
For h=8: shift ≈ 8.077 (barely perceptible but adds warmth)

For a single-cycle wavetable, inharmonicity creates phase inconsistency across cycles —
acceptable for the M1 character (the M1's PCM loop point artifact is similar).

For frame f, t = f/15.0:
  Harmonic amplitudes:
    A[1] = 1.0                           // fundamental — always dominant
    A[2] = 0.55 * (0.6 + 0.4 * t)       // 2nd: grows slightly with velocity
    A[3] = 0.38 * (0.5 + 0.5 * t)       // 3rd: grows, the M1 "body"
    A[4] = 0.28 * t^0.5                  // 4th: appears with velocity (attack clank)
    A[5] = 0.20 * t                      // 5th: velocity-dependent
    A[6] = 0.14 * t^1.5                  // 6th: mainly at high velocity
    A[7] = 0.10 * t^2                    // 7th: only at high velocity
    A[8] = 0.07 * t^2                    // 8th: only at high velocity

  Piano inharmonicity (partial detuning via phases):
    Phase offset for h=4: 0.06 * t (slight drift from perfect harmonic)
    This creates the "alive" quality of piano strings vs. pure additive.
```

**Implementation spec:**

```cpp
static Wavetable makeM1PianoSpec()
{
    Wavetable wt (16);
    const double twoPi = 2.0 * 3.14159265358979323846;
    const int N = wt.frameSize_;
    for (int f = 0; f < 16; ++f)
    {
        const double t = (double) f / 15.0;
        // Piano inharmonicity constant (middle C range)
        constexpr double B = 0.00015;
        for (int i = 0; i < N; ++i)
        {
            const double phase = twoPi * (double) i / (double) N;
            // Apply inharmonicity: harmonic h sits at h * sqrt(1 + B*h*h)
            auto inharm = [&](int h) -> double {
                return (double) h * std::sqrt (1.0 + B * (double) h * (double) h);
            };
            double s =
                1.00                                   * std::sin (inharm(1) * phase)
              + 0.55 * (0.6 + 0.4 * t)                * std::sin (inharm(2) * phase)
              + 0.38 * (0.5 + 0.5 * t)                * std::sin (inharm(3) * phase)
              + 0.28 * std::sqrt (t)                   * std::sin (inharm(4) * phase)
              + 0.20 * t                               * std::sin (inharm(5) * phase)
              + 0.14 * std::pow (t, 1.5)               * std::sin (inharm(6) * phase)
              + 0.10 * t * t                           * std::sin (inharm(7) * phase)
              + 0.07 * t * t                           * std::sin (inharm(8) * phase);
            wt.sampleRef (f, i) = (float)(s * 0.40);
        }
    }
    return wt;
}
```

**Sonic character test:** Frame 0 should sound like a very soft, almost gentle piano tone — mostly fundamental with a slight 2nd harmonic warmth. Frame 7-8 should be the classic M1 Piano sound — bright, present, slightly "honky" in the mid-upper register, unmistakably that 80s digital piano character. Frame 15 should sound like a hard-struck M1 key — attack clank, lots of upper harmonic content, the percussive "clank" that made the M1 Piano perfect for house music and pop stabs. The piano inharmonicity (B=0.00015 in the inharm() function) is the mathematical key: it ensures it sounds like piano strings and NOT like a simple additive sawtooth.

---

## Experimental Category

---

### 5. Dustbowl

**Sonic identity:**

"Dustbowl" evokes the American Great Depression era (1930-1936): field recordings on 78rpm shellac discs, early microphone and amplifier technology, the specific sonic character of recordings made on carbon microphones with limited frequency response and heavy surface noise. Research on 78rpm records shows:
- Maximum theoretical bandwidth: 8-12 kHz (determined by stylus size and shellac composition)
- Early 1930s recordings often achieve only 4-6 kHz bandwidth (carbon microphone limit)
- The recording equalization of early 78s applied a bass-cut above ~300 Hz → playback sounds thin below 300 Hz, boosted in the 1-4 kHz presence range
- Surface noise is predominantly 2-8 kHz (shellac grain noise) — not white noise but spectrally shaped
- The "crackle" character is impulsive, not random — occasional click events

**Harmonic signature:**

Current implementation (line 1118): saw + high-passed dust noise, noise blend 0→40%. This is correct in concept. Rework needs:
1. The saw should have LP filter applied (simulate the 4-8 kHz bandwidth limit of the recording medium)
2. The noise should be mid-band (NOT high-passed white noise — 78rpm surface noise is mid-frequency shaped, 1-6 kHz)
3. The WT POS sweep should go from "clean 78rpm with surface noise" at frame 0 to "degraded, damaged record" at frame 15, not just more noise

**WT POS sweep design:**

- Frame 0: bandlimited saw (cutoff≈h=20, representing ~4.4 kHz at A=220 Hz) + very light noise
- Frame 3-4: slightly wider saw (h=24) + light surface noise
- Frame 8: mid-bandwidth saw (h=32) + moderate noise + phase drift on upper harmonics (represents varispeed instability of early turntables)
- Frame 12: broader saw (h=40) + heavier noise
- Frame 15: near-full saw (h=48) + heavy surface noise + strong upper-harmonic phase randomization (worn record played too many times)

**Mathematical spec:**

```
For frame f, t = f/15.0:
  LP cutoff harmonic: cutH(f) = round(18 + 30*t)    // 18 → 48 (narrow LP to wide)
  For h = 1..48:
    // Saw with LP rolloff
    if h <= cutH: A[h] = 1/h * (1 - 0.3*(h/cutH)^2)  // gentle LP shape
    else:         A[h] = 0

  Noise floor: add harmonics h=15..30 with randomized phases AND amplitude 0.02..0.08*t

  Phase randomization on upper harmonics (varispeed jitter):
    For h >= 12: phase[h] = t * (deterministic_random[h] * 0.8)
    // Randomizes phases of upper harmonics proportional to frame position
    // This creates the characteristic "warbling" of old records
```

**Implementation spec (spec-based, uses buildFromSpec for anti-aliasing):**

```cpp
static WavetableSpec makeDustbowlSpec()
{
    WavetableSpec spec;
    // Pre-generate deterministic random phases and noise amplitudes (varispeed + crackle)
    std::uint32_t rng = 0xFEEDFACEu;
    auto nextF = [&rng]() -> float {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) rng / (float) 0xFFFFFFFFu;
    };
    float randPhase[48] = {}, randNoise[48] = {};
    for (int i = 0; i < 48; ++i) {
        randPhase[i] = nextF() * 6.28318530718f;
        randNoise[i] = 0.02f + nextF() * 0.06f;  // 0.02..0.08
    }

    for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
    {
        const float t    = (float) f / 15.0f;
        const int cutH   = juce::jlimit (18, 48, (int) std::round (18.0f + 30.0f * t));
        FrameSpec& fs    = spec.frames[(size_t) f];
        fs.numHarmonics  = 48;

        for (int h = 1; h <= 48; ++h)
        {
            // LP-filtered saw (simulates 78rpm bandwidth limit)
            float amp = 0.0f;
            if (h <= cutH)
            {
                const float norm = (float) h / (float) cutH;
                amp = (1.0f / (float) h) * (1.0f - 0.3f * norm * norm);
            }
            // Mid-band surface noise (harmonics 12-30): grows with frame
            if (h >= 12 && h <= 30)
                amp += t * randNoise[h - 1];

            fs.amplitudes[(size_t)(h - 1)] = amp;

            // Varispeed phase jitter on upper harmonics (starts at h=10)
            if (h >= 10)
                fs.phases[(size_t)(h - 1)] = t * randPhase[h - 1] * 0.8f;
        }
    }
    return spec;
}
```

**Sonic character test:** Frame 0 should sound like a clean vintage recording — saw-wave body with limited bandwidth, perhaps 4-5 kHz, soft and slightly muffled. Frame 7-8 should be a 78rpm record with noticeable surface hiss and slight pitch instability in the upper harmonics. Frame 15 should be a heavily worn record — clear surface noise, upper harmonic warble from phase randomization, but still recognizably musical (the fundamental and low harmonics are still clean). The character should be immediately identifiable as "vintage, lo-fi, worn" and completely different from any analog saw or experimental noise-based table.

---

### 6. StaticEvolve

**Sonic identity:**

"StaticEvolve" is the concept of something that starts as pure static (radio static between stations — white noise) and gradually resolves into structured tonal content. This is a strongly narrative wavetable — it tells a sonic story. The existing implementation (line 1154) does this correctly: cleanAmt × tone + (1-cleanAmt) × noise. However it uses a 30-harmonic saw as the "tone" target, which is mundane. The rework should target a more evocative tone: something like a chorus of voices or a rich pad — something that feels like a radio signal being tuned in. The arrival should be REWARDING.

**WT POS sweep design:**

- Frame 0: pure white noise (random amplitude, random phases on all harmonics 1-64) — absolute static
- Frame 3: noise dominant (80%) + emerging formant-like tones barely audible under noise
- Frame 7: noise and tone roughly equal — the "half-tuned" station effect where you can ALMOST make out a voice or instrument
- Frame 11: mostly tone (70%), noise as a texture layer — the station is nearly locked in
- Frame 15: almost pure harmonic tone with just a thin noise floor — the signal has arrived; it reveals a rich vowel-formant chord (the "reward")

**The target tone (frame 15):** A major chord formant structure — three voices at formants for "ah" vowel (730/1090/2440 Hz for a 220 Hz fundamental) → feels like a human choir resolving out of static. This is MORE emotionally compelling than just a saw wave.

**Mathematical spec:**

```
For frame f, t = f/15.0:
  toneAmt(f) = t^2                        // quadratic: noise dominates early, tone arrives suddenly late
  noiseAmt(f) = (1 - t)^1.5              // faster noise decay than tone arrival (feels like "breaking through")

  Noise component: harmonics 1..64, each with:
    amplitude = noiseAmt * (0.2 + rand01() * 0.8)  // deterministic per frame
    phase     = rand01() * 2π                        // deterministic per frame

  Tone component: choir/formant-based (3 formant peaks at choir "ah" frequencies)
    For h = 1..48:
      freq = h * 220.0 Hz (reference fundamental)
      amp_tone = formantAmp(freq, 730, 1090, 2440)  // the existing formantAmp() helper
              / (float)h                              // 1/h rolloff
      amplitude += toneAmt * amp_tone

  Final: mix tone + noise, clamp, normalize per frame
```

**Implementation spec:**

```cpp
static WavetableSpec makeStaticEvolveSpec()
{
    WavetableSpec spec;
    constexpr int kNH = 64;

    for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
    {
        const float t       = (float) f / 15.0f;
        const float toneAmt = t * t;                         // quadratic arrival
        const float noiseAmt = std::pow (1.0f - t, 1.5f);   // faster noise decay

        // Deterministic per-frame random noise (different character each frame)
        std::uint32_t rng = 0x12345678u + (std::uint32_t) f * 0x9E3779B9u;
        auto nxt = [&rng]() -> float {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return (float) rng / (float) 0xFFFFFFFFu;
        };

        FrameSpec& fs = spec.frames[(size_t) f];
        fs.numHarmonics = kNH;

        for (int h = 1; h <= kNH; ++h)
        {
            // Noise contribution (deterministic random amplitude + phase)
            const float noiseAmp = noiseAmt * (0.2f + nxt() * 0.8f);
            const float noisePh  = nxt() * 6.28318530718f;

            // Tone contribution: choir "ah" vowel formants at A3=220Hz
            const float freq = (float) h * 220.0f;
            const float sigma = 90.0f;
            auto gf = [&](float fc) -> float {
                const float d = freq - fc;
                return std::exp (-(d * d) / (2.0f * sigma * sigma));
            };
            const float toneAmp = toneAmt * (gf (730.0f) + 0.7f * gf (1090.0f)
                                            + 0.45f * gf (2440.0f)) / (float) h;
            // Mix noise and tone; blend phases
            const float totalAmp = noiseAmp + toneAmp;
            // Phase: noise frame → coherent zero at full tone
            const float ph = noiseAmt * noisePh;   // phase randomizes when noisy, zeroes as tone arrives

            fs.amplitudes[(size_t)(h - 1)] = totalAmp;
            fs.phases[(size_t)(h - 1)]     = ph;
        }
    }
    return spec;
}
```

**Sonic character test:** Frame 0 should sound like pure static — no tonal center, incoherent, random. Frame 4-5 should have an almost-subliminal tonal suggestion barely perceptible under noise. Frame 10-11 should sound like "almost there" — the choir is resolving but still texturally rough. Frame 15 should be the payoff: a recognizable vowel formant choir sound with just a thin noise floor, like a radio station locking in. The phase randomization collapsing to zero as tone arrives is crucial — without it, the noise component persists as spectral incoherence even when the amplitude is low.

---

### 7. SpectralDrift

**Sonic identity:**

"SpectralDrift" is philosophically different from all other tables: SAME harmonic amplitude spectrum across all frames, but DIFFERENT phase relationships. The result is that the frequency-domain power spectrum is identical at every frame position — in a spectrum analyzer it would look the same — but the waveform shape and therefore the ear's perception of the sound's character changes completely.

The existing implementation (line 1184) does this correctly with a 32-harmonic saw + per-frame random phases. What needs reworking: the random phases are fully random per frame (no interpolation), so scanning WT POS sounds like jumping between unrelated phase states rather than "drifting." The rework should use linearly interpolated phases from a coherent state (frame 0 = all phases zero) to a maximally randomized state (frame 15 = fully random), so scanning WT POS feels like a smooth, unpredictable drift. This is exactly the `PhaseDrift` concept from Phase 11h/j — applied to the Experimental category with the SpectralDrift name.

**Why it's distinct from PhaseDrift (in Morph category):**

PhaseDrift (Morph category) uses a 1/h amplitude spectrum. SpectralDrift should use a DIFFERENT amplitude distribution to sound distinct while using the same phase-drifting concept:
- SpectralDrift uses a flat spectral envelope (equal amplitude at all harmonics h=1..32)
- This makes it sound more "noisy" / "FM-like" even at frame 0 (which sounds like a complex additive tone rather than a saw)
- The phase drift effect is more dramatic on a flat spectrum than on a 1/h spectrum

**WT POS sweep design:**

- Frame 0: all harmonics h=1..32 at equal amplitude, all phases = 0 → buzzy, aggressive, equal-amplitude additive tone
- Frame 5: phases drifting 33% toward random targets → starting to shimmer and spread
- Frame 10: phases 67% toward random → smeary, diffuse, the spectrum spreads laterally
- Frame 15: fully randomized phases → same spectral energy but sounds like something between pink noise and an aggressive pad

**Mathematical spec:**

```
Pre-generate target phases (deterministic):
  targetPhase[h] for h=1..32: xorshift32 sequence, seeded 0xDEADC0DEu

For frame f, t = f/15.0:
  For h = 1..32:
    A[h] = 1.0 / sqrt(32)         // flat spectrum, equal amplitude, normalized RMS
    P[h] = t * targetPhase[h]     // linear phase interpolation: 0 → fully random

numHarmonics = 32
```

**Implementation spec:**

```cpp
static WavetableSpec makeSpectralDriftSpec()
{
    WavetableSpec spec;
    // Deterministic target phases for frame 15 (maximally randomized)
    std::uint32_t rng = 0xDEADC0DEu;
    auto nextPh = [&rng]() -> float {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return ((float) rng / (float) 0xFFFFFFFFu) * 6.28318530718f;
    };
    float targetPhases[32] = {};
    for (int h = 0; h < 32; ++h) targetPhases[h] = nextPh();

    constexpr int kNH = 32;
    const float normAmp = 1.0f / std::sqrt ((float) kNH);  // equal RMS normalization

    for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
    {
        const float t = (float) f / 15.0f;
        FrameSpec& fs = spec.frames[(size_t) f];
        fs.numHarmonics = kNH;
        for (int h = 1; h <= kNH; ++h)
        {
            fs.amplitudes[(size_t)(h - 1)] = normAmp;
            fs.phases[(size_t)(h - 1)]     = t * targetPhases[h - 1];
        }
    }
    return spec;
}
```

**Sonic character test:** Frame 0 should sound like an aggressive, buzzy additive tone — all 32 harmonics at equal amplitude creates a very complex, almost nasal-aggressive sound (it's NOT a saw because the high harmonics are equally loud). Slowly scanning toward frame 15, the sound should increasingly shimmer and spread — like the same complex tone being put through a random IIR comb filter that evolves. Frame 15 should sound like musical noise — not truly random (you can still hear pitch) but the phase incoherence makes it feel unstable and shifting. The distinct quality: it never changes timbre, only character. This makes it extremely useful for adding "life" to pads and drones without changing the mix frequency content.

---

### 8. SerumHD

**Sonic identity:**

"Serum HD" (HD = High Definition) represents the quality standard of modern wavetable synthesis as established by Xfer Records Serum. Serum's brand promise: crystal-clear aliasing-free audio, maximum harmonic density, modern production-ready brightness. The "HD" character in Serum means:
- Full harmonic spectrum up to Nyquist, properly bandlimited (no aliasing)
- Bright spectral tilt: even harmonics receive a boost (15% in current implementation)
- Gaussian spectral envelope CENTERED at a high harmonic (not at h=1 like analog saws)
- Clean, zero-phase waveform (no phase randomization — anti-aliased perfection)
- The "HD" sweep should go from a clean, relatively warm modern tone to an almost overwhelmingly bright, harmonically dense modern synth sound

**Current implementation issues:**

Existing `makeSerumHD()` (line 1213): Gaussian center migrates from h=2 to h=10 across 16 frames. This range (h=2 to h=10) is too narrow — it sounds like a slightly-shifting saw, not the "HD brightness" Serum is known for. The rework should use the `buildFromSpec()` path (for proper mip-based anti-aliasing) and push the spectral brightness much further.

**WT POS sweep design:**

- Frame 0: Gaussian centered at h=3, sigma=4 → warm, full modern saw analog-ish quality (harmonics h=1-8 dominant)
- Frame 4: center shifts to h=8, sigma=5 → bright, present, strong upper mids
- Frame 8: center h=16, sigma=6 → very bright, like a fully opened high-quality VCF
- Frame 12: center h=28, sigma=7 → extreme presence, almost like a comb filter position
- Frame 15: center h=45, sigma=8 → maximum HD brilliance — like a crystal-clear synth brass stab with full harmonic density up high; still musical because the Gaussian envelope keeps it from being just noise

**Mathematical spec:**

```
For frame f, t = f/15.0:
  center(f) = 3.0 + 42.0 * t^1.5       // 3 → 45, cubic acceleration for drama
  sigma(f)  = 4.0 + 4.0  * t            // 4 → 8 (gets wider for density)
  For h = 1..96:
    evenBoost = (h % 2 == 0) ? 1.20 : 1.0   // 20% even-harmonic boost (Serum character)
    A[h] = evenBoost * exp(-(h - center)^2 / (2*sigma^2))
  numHarmonics = 96
```

**Implementation spec:**

```cpp
static WavetableSpec makeSerumHDSpec()
{
    WavetableSpec spec;
    constexpr int kNH = 96;

    for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
    {
        const float t      = (float) f / 15.0f;
        const float tCube  = t * std::sqrt (t);               // t^1.5 for dramatic acceleration
        const float center = 3.0f + 42.0f * tCube;           // 3 → 45
        const float sigma  = 4.0f + 4.0f  * t;               // 4 → 8
        FrameSpec& fs      = spec.frames[(size_t) f];
        fs.numHarmonics    = kNH;

        for (int h = 1; h <= kNH; ++h)
        {
            const float d         = (float) h - center;
            const float evenBoost = (h % 2 == 0) ? 1.20f : 1.0f;  // Serum even-harmonic brightness
            fs.amplitudes[(size_t)(h - 1)] = evenBoost
                                           * std::exp (-(d * d) / (2.0f * sigma * sigma));
            fs.phases[(size_t)(h - 1)]     = 0.0f;  // HD = phase-coherent, clean
        }
    }
    return spec;
}
```

**Sonic character test:** Frame 0 should sound like a high-quality modern synthesizer saw — bright, clean, present, but not extreme. Frame 7-8 should be noticeably brighter than any analog category table at equivalent WT POS — this is "HD" quality, like listening to a well-mastered modern synth through a transparent system. Frame 15 should be the brightest sound in the entire bank — an overwhelming harmonic density centered in the upper-mid to high register, maximum modern synthesizer brilliance, zero phase artifacts. The even-harmonic 20% boost gives it the slightly "buzzy" vs "hollow" quality that separates modern digital from classic analog (which tends to be neutral or odd-harmonic-emphasizing).

---

## Cross-category comparison summary

### How digital sounds different from analog

Analog (ProphetSaw, OBXSaw, JunoStr) is warm, ladder-harmonic (1/h), optionally with phase scatter for chorus/detuning. The harmonic content is CONTINUOUS — every integer harmonic is present, decaying smoothly.

Digital is characterized by DISCONTINUITY and SPECIFICITY:
- PPGWave: Gaussian harmonic peak at a specific register, not a smooth 1/h ladder. Moving WT POS sounds like changing which tube you're blowing through — the resonant character is completely different at each position.
- DX7EP: FM sidebands at non-integer ratios (3.5×, 7×, 10.5×) — harmonics at positions that don't exist in any natural additive synthesis. The FM modulation index sweep creates a fundamentally different kind of timbral change than adding harmonics.
- D50Bell: No continuous harmonic ladder at all — ONLY 5 inharmonic partials. Everything else is silence. The bell decay model (fundamentals dying, upper partials sustaining) is the opposite of how analog envelopes work.
- M1Piano: Piano inharmonicity (B constant) creates harmonics that are SLIGHTLY off-integer — h=4 is actually at 4.01×, h=8 at 8.08×. This makes it sound "like a real instrument" rather than "like a synth," even though it's additive. The WT POS velocity model (soft → hard) is a completely different axis than analog's spectrum changes.

### How experimental sounds different from everything else

Experimental is about PROCESS OVER PITCH — the wavetables are defined by what happens to sound rather than what the sound inherently is:
- Dustbowl: A TIME MACHINE. The process is "vinyl degradation" — bandwidth limiting + surface noise + varispeed phase jitter. The frame sweep is physically grounded in how 78rpm records degrade, not in harmonic content theory.
- StaticEvolve: A NARRATIVE ARC. It tells a story: static → signal. No other wavetable in the bank has a story. When you set WT POS to an LFO, you literally hear signal tuning in and out, which is compositionally unique.
- SpectralDrift: A PSYCHOACOUSTIC PARADOX. Identical spectrum at every frame (a spectrum analyzer would show the same curve) but completely different sound character. This is the kind of wavetable that makes audio engineers say "that's impossible" before hearing it — it demonstrates phase as a completely independent dimension of timbre.
- SerumHD: A STATEMENT OF INTENT. It's not evocative — it's aspirational. "This is what maximum modern wavetable synthesis sounds like." The sweep is not about the past (like Dustbowl) or about process (like the others) — it's about pushing FORWARD, farther up the harmonic series than any other table goes.

---

## Implementation priority and integration notes

### Which tables can use buildFromSpec() (mip anti-aliasing):
- PPGWave: YES — `makePPGWaveSpec()` replaces `makePPGWave()`
- DX7EP: NO — non-integer partials (3.5×, 7×) require time-domain legacy constructor
- D50Bell: NO — non-integer inharmonic ratios require legacy constructor
- M1Piano: NO — piano inharmonicity B constant requires time-domain (fractional harmonic positions)
- Dustbowl: YES — `makeDustbowlSpec()` replaces `makeDustbowl()`
- StaticEvolve: YES — `makeStaticEvolveSpec()` replaces `makeStaticEvolve()`
- SpectralDrift: YES — `makeSpectralDriftSpec()` replaces `makeSpectralDrift()`
- SerumHD: YES — `makeSerumHDSpec()` replaces `makeSerumHD()`

### Build time budget:
- 4 new spec-built tables × ~175ms = ~700ms added startup (within Phase 10a budget)
- 3 legacy time-domain tables: ~10ms each (negligible)

### WavetableBank enum changes:
The existing Digital and Experimental enum entries stay in place — only their factory method implementations change. No preset compatibility issues.

### Per-frame normalization note:
SpectralDrift (flat equal-amplitude spectrum) will sound louder at frame 0 (coherent phases = high peak) than at frame 15 (random phases = lower peak despite same RMS). The existing `normalizeMipLevels()` normalizes ACROSS ALL FRAMES by global peak, which means frame 15 sounds relatively quieter. Add a per-frame peak normalization pass before calling `buildFromSpec()` if this level imbalance is perceptually problematic. For SpectralDrift specifically, frame 0's peak will be approximately sqrt(32) × normAmp ≈ 1.0 (already set), but the random-phase frame 15's peak will be approximately sqrt(sum of normAmp^2 × 32) × 0.9 ≈ 0.9 — only a ~10% difference, acceptable.
