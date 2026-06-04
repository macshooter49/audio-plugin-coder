# Vowel Formants + Metallic Partials Research — Phase 11l

## Sources

- Peterson & Barney (1952), "Control methods used in a study of the vowels," JASA 24(2):175–184 — canonical American English vowel formant reference
- Hillenbrand, J., Getty, L., Clark, M., Wheeler, K. (1995), "Acoustic characteristics of American English vowels," JASA 97:3099–3111 — updated F1/F2/F3 data for 139 speakers
- Klatt, D.H. (1980), "Software for a cascade/parallel formant synthesizer," JASA 67:971–995 — synthesis filter bandwidths; [PDF](https://www.fon.hum.uva.nl/david/ma_ssp/doc/Att-1980-JAS000971.pdf)
- Klatt formant synthesis parameters wiki: https://linguistics.berkeley.edu/plab/guestwiki/index.php?title=Klatt_Synthesizer_Parameters
- Praat manual — Peterson & Barney formant table: https://www.fon.hum.uva.nl/praat/manual/Create_formant_table__Peterson___Barney_1952_.html
- Praat KlattGrid vowel parameters: https://www.fon.hum.uva.nl/praat/manual/Create_KlattGrid_from_vowel___.html
- Static Measurements of Vowel Formant Frequencies and Bandwidths (PMC review): https://pmc.ncbi.nlm.nih.gov/articles/PMC6002811/
- VoiceScience.org formant lexicon: https://www.voicescience.org/lexicon/formant/
- Ternström et al. — Formant frequencies in choir singers: https://www.researchgate.net/publication/252590389_Formant_frequencies_of_choir_singers/
- Catford (2001) "A Practical Introduction to Phonetics" — male F1/F2 table, via Wikipedia Formant article
- CCRMA Stanford — Percussion acoustics: https://ccrma.stanford.edu/CCRMA/Courses/152/percussion.html
- PSU Acoustics — Flexural bar modes (exact ratios): https://www.acs.psu.edu/drussell/Demos/Flexural-Bar/flexural.html
- Euphonics 3.2.1 — Free-free bending beams: https://euphonics.org/3-2-1-bending-beams-and-free-free-modes/
- Gibson/UConn — Vibrating bars: https://www.phys.uconn.edu/~gibson/Notes/Section4_1/Sec4_1.htm
- Wikipedia — Vibraphone: https://en.wikipedia.org/wiki/Vibraphone
- Wikipedia — Strike tone (bell partials): https://en.wikipedia.org/wiki/Strike_tone
- Rossing — Acoustics of Glass Harmonicas (ASA 147th lay paper): https://acoustics.org/pressroom/httpdocs/147th/Rossing_Harmonicas1.htm
- Hibberts — Musical sound quality of church bells: https://www.hibberts.co.uk/the-musical-sound-quality-of-church-bells/

---

## Part A — Vowel Formants

### Background: what makes vowels sound like vowels

Vowel identity is primarily determined by two resonant peaks in the vocal tract transfer function:
- **F1** (first formant): correlates with vowel height. Low F1 = close/high vowels (/i/, /u/). High F1 = open/low vowels (/a/).
- **F2** (second formant): correlates with vowel backness. High F2 = front vowels (/i/, /e/). Low F2 = back vowels (/o/, /u/).
- **F3** adds timbre richness and helps define "r-coloring." For choir synthesis, F3 contributes heavily to the "ring."
- **F4** (singer's formant region): around 2.5–3.5 kHz, where F3+F4+F5 cluster in trained voices to project over orchestras.

The current wavetable architecture generates harmonic content by frequency-domain spec. To implement formant shaping, we compute a **formant weight** for each harmonic `h` at frequency `h * F0` using the sum of Gaussian (or Lorentzian) resonance curves centered at F1, F2, F3, F4.

### Reference table — Mean formant frequencies (Hz)

Synthesized from Peterson & Barney (1952), Hillenbrand et al. (1995), Catford (2001), and Klatt (1980). IPA symbols are American English approximations.

**Male voice (F0 ≈ 110–130 Hz, vocal tract ~17 cm)**

| Vowel | IPA example | F1 | F2 | F3 | F4 | BW1 | BW2 | BW3 |
|---|---|---|---|---|---|---|---|---|
| /a/ | "father" | 730 | 1090 | 2440 | 3300 | 80 | 90 | 120 |
| /e/ | "bed" (/ɛ/) | 530 | 1840 | 2480 | 3300 | 60 | 80 | 100 |
| /i/ | "see" (/iː/) | 270 | 2290 | 3010 | 3300 | 50 | 80 | 100 |
| /o/ | "boat" (/oʊ/) | 570 | 840 | 2410 | 3300 | 60 | 80 | 100 |
| /u/ | "boot" (/uː/) | 300 | 870 | 2240 | 3300 | 60 | 80 | 100 |

**Female voice (F0 ≈ 200–260 Hz, shorter vocal tract ~14 cm → formants ~15–20% higher)**

| Vowel | IPA example | F1 | F2 | F3 | F4 | BW1 | BW2 | BW3 |
|---|---|---|---|---|---|---|---|---|
| /a/ | "father" | 850 | 1220 | 2810 | 3700 | 90 | 100 | 130 |
| /e/ | "bed" (/ɛ/) | 610 | 2060 | 2840 | 3700 | 70 | 90 | 110 |
| /i/ | "see" (/iː/) | 310 | 2790 | 3310 | 3700 | 60 | 90 | 110 |
| /o/ | "boat" (/oʊ/) | 650 | 960 | 2770 | 3700 | 70 | 90 | 110 |
| /u/ | "boot" (/uː/) | 370 | 950 | 2670 | 3700 | 70 | 90 | 110 |

**Notes on these numbers:**
- /a/ (ah): Highest F1 of all vowels (mouth maximally open). F2 moderate. This is the "bright open" vowel.
- /e/ (eh/ɛ): Mid-high F1, very high F2 (front position of tongue).
- /i/ (ee): Lowest F1 (tongue close to palate), highest F2 of any vowel — the "smiling" position.
- /o/ (oh): Mid F1, low F2 (tongue backed, lips rounded).
- /u/ (oo): Low F1 (like /i/), low F2 (like /o/) — the corner vowel that is both close AND back.
- BW values: typical adult spoken ranges are B1 = 50–140 Hz, B2 = 62–149 Hz, B3 = 67–223 Hz (PMC6002811). Using midrange values here. Sung voice bandwidths are slightly narrower (more sustained resonance).

**Q factors implied (F/BW):**
- /i/ F2: Q = 2290/80 ≈ 29 — narrow, very distinctive
- /a/ F1: Q = 730/80 ≈ 9 — broad, open
- /u/ F1: Q = 300/60 = 5 — broad, subdued

### Sung vs. spoken differences

Research (Ternström choir study; formant tuning literature) shows:
1. **F1/F2 identities are preserved** in choir singing at moderate pitches. Singers must keep F1 > F0 to maintain vowel identity; above ~500 Hz fundamental (soprano upper register), F1 must be actively raised, causing vowel neutralization.
2. **Bandwidths narrow slightly** in sustained sung tone: B1 can drop to 40–60 Hz range for a clear choir vowel, making formants more distinct.
3. **Singer's formant cluster** (F3+F4+F5 near 2.5–3.5 kHz) appears in male trained voices — adds "ring" that cuts through orchestral texture. Our ChoirAtoO should include modest energy at h≈10–15 to simulate this.
4. **For choir wavetable synthesis at 220 Hz fundamental:** h=1 at 220 Hz, h=2 at 440 Hz, h=3 at 660 Hz, h=4 at 880 Hz, etc. F1 for /a/ is at 730 Hz ≈ between h=3 (660) and h=4 (880). F2 for /a/ at 1090 Hz ≈ between h=4 (880) and h=5 (1100).

### Formant weight function for WT spec generation

For each harmonic h at frequency `freq = h * F0`, compute amplitude as sum of Lorentzian (resonance) peaks:

```cpp
// Returns the formant-weighted amplitude for harmonic h at frequency freq_hz
// F[] = {F1, F2, F3, F4}, BW[] = {BW1, BW2, BW3, BW4}
// spectralSlope: typically -0.7 to -1.0 (roll-off per octave)
float formantWeight(float freq_hz,
                    float F1, float F2, float F3, float F4,
                    float BW1, float BW2, float BW3, float BW4,
                    float spectralSlope = -0.7f)
{
    // Lorentzian resonance: L(f) = (BW/2)^2 / ((f-Fc)^2 + (BW/2)^2)
    auto L = [](float f, float Fc, float BW) {
        float r = BW * 0.5f;
        return (r * r) / ((f - Fc) * (f - Fc) + r * r);
    };
    float weight = L(freq_hz, F1, BW1)
                 + 0.8f * L(freq_hz, F2, BW2)
                 + 0.5f * L(freq_hz, F3, BW3)
                 + 0.3f * L(freq_hz, F4, BW4);
    // spectral slope: voiced source rolls off ~-12 dB/octave, partial that at -0.7 to taste
    weight *= std::pow(freq_hz, spectralSlope);
    return weight;
}
```

### Implementation spec for each vocal wavetable

#### ChoirAtoO — A→O sweep (16 frames)

Intended character: starts with bright, open /a/ "ah" sound, morphs through intermediate states to dark, rounded /o/ "oh." Both are common choir vowels. The sweep should feel like a sustained "aah…ooh" breath.

At F0 = 220 Hz (for male choir register — synth voice pitch will determine actual F0 but the WT stores one canonical version):

```cpp
static WavetableSpec makeChoirAtoOSpec()
{
    // Formant targets (male voice register)
    // Frame 0 = /a/:  F1=730, F2=1090, F3=2440
    // Frame 15 = /o/: F1=570, F2=840,  F3=2410
    // Linear interpolation across 16 frames.
    
    // Singer's formant bonus: add 0.3f × L(freq, 3100, 200) to all frames
    // simulates trained male choir ring at ~3 kHz.
    
    constexpr float F0 = 220.0f;  // canonical fundamental for spec generation
    constexpr int   numH = 64;    // 64 harmonics sufficient for this range
    
    // /a/ parameters
    const float a_F1=730, a_F2=1090, a_F3=2440, a_F4=3300;
    const float a_B1=80,  a_B2=90,   a_B3=120,  a_B4=200;
    
    // /o/ parameters
    const float o_F1=570, o_F2=840,  o_F3=2410, o_F4=3300;
    const float o_B1=60,  o_B2=80,   o_B3=100,  o_B4=200;
    
    WavetableSpec spec;
    for (int frame = 0; frame < WavetableSpec::kNumFrames; ++frame)
    {
        float t = (float)frame / (float)(WavetableSpec::kNumFrames - 1); // 0..1
        float F1  = a_F1 + t * (o_F1 - a_F1);
        float F2  = a_F2 + t * (o_F2 - a_F2);
        float F3  = a_F3 + t * (o_F3 - a_F3);
        float F4  = a_F4 + t * (o_F4 - a_F4);
        float BW1 = a_B1 + t * (o_B1 - a_B1);
        float BW2 = a_B2 + t * (o_B2 - a_B2);
        float BW3 = a_B3 + t * (o_B3 - a_B3);
        float BW4 = a_B4 + t * (o_B4 - a_B4);
        
        FrameSpec& fs = spec.frames[frame];
        fs.numHarmonics = numH;
        for (int h = 1; h <= numH; ++h)
        {
            float freq = (float)h * F0;
            if (freq > 8000.0f) break;  // above audible formant range
            
            auto L = [](float f, float Fc, float BW) {
                float r = BW * 0.5f;
                return (r * r) / ((f-Fc)*(f-Fc) + r*r);
            };
            float w = L(freq,F1,BW1)
                    + 0.8f * L(freq,F2,BW2)
                    + 0.5f * L(freq,F3,BW3)
                    + 0.25f * L(freq,F4,BW4)
                    + 0.3f * L(freq,3100.0f,200.0f); // singer's formant ring
            w *= std::pow((float)h, -0.7f);  // spectral slope
            fs.amplitudes[h-1] = w;
            fs.phases[h-1] = 0.0f;  // cosine phases for stable morph
        }
    }
    return spec;
    // buildFromSpec normalizes peak to 1.0 — formant SHAPE is what matters, not scale
}
```

**Key authentication notes:**
- /a/ has F1 at 730 Hz — for 220 Hz fundamental, this falls between h=3 (660 Hz) and h=4 (880 Hz). Both will get energy; the formant peak "straddles" two harmonics.
- /o/ F2 drops from 1090 to 840 Hz, which is below h=4 (880 Hz). This creates the "darker" quality.
- The singer's formant ring at 3100 Hz adds presence across the whole sweep — authentically choral.

---

#### VowelMorph — A→E→I→O→U sweep (16 frames)

Maps all 5 vowels across 16 frames: frames 0–2 = /a/, frames 4–6 = /e/, frames 7–9 = /i/, frames 11–13 = /o/, frames 14–15 = /u/. Crossfade in between.

```cpp
static WavetableSpec makeVowelMorphSpec()
{
    // 5 formant triplets (male register)
    struct VowelParams { float F1,F2,F3,F4, BW1,BW2,BW3; };
    constexpr VowelParams vowels[5] = {
        // /a/ "ah"    F1    F2    F3    F4    B1   B2   B3
        {              730, 1090, 2440, 3300,  80,  90, 120 },
        // /e/ "eh"
        {              530, 1840, 2480, 3300,  60,  80, 100 },
        // /i/ "ee"
        {              270, 2290, 3010, 3300,  50,  80, 100 },
        // /o/ "oh"
        {              570,  840, 2410, 3300,  60,  80, 100 },
        // /u/ "oo"
        {              300,  870, 2240, 3300,  60,  80, 100 },
    };
    
    // Map 16 frames to 5 vowels: each vowel occupies 3.2 frames worth of space
    // Segment boundaries: a=0..2.5, e=3.2..5.7, i=6.4..8.9, o=9.6..12.1, u=12.8..15
    
    constexpr float F0 = 220.0f;
    constexpr int   numH = 64;
    
    WavetableSpec spec;
    for (int frame = 0; frame < WavetableSpec::kNumFrames; ++frame)
    {
        float t = (float)frame / (float)(WavetableSpec::kNumFrames - 1); // 0..1 over 5 vowels
        float vIdx = t * 4.0f;  // 0..4 indexes into vowels[]
        int   v0   = (int)vIdx;
        int   v1   = std::min(v0 + 1, 4);
        float vFrac = vIdx - (float)v0;
        
        // Interpolate all formant params between adjacent vowels
        auto interp = [&](float a, float b) { return a + vFrac * (b - a); };
        float F1  = interp(vowels[v0].F1,  vowels[v1].F1);
        float F2  = interp(vowels[v0].F2,  vowels[v1].F2);
        float F3  = interp(vowels[v0].F3,  vowels[v1].F3);
        float F4  = interp(vowels[v0].F4,  vowels[v1].F4);
        float BW1 = interp(vowels[v0].BW1, vowels[v1].BW1);
        float BW2 = interp(vowels[v0].BW2, vowels[v1].BW2);
        float BW3 = interp(vowels[v0].BW3, vowels[v1].BW3);
        
        FrameSpec& fs = spec.frames[frame];
        fs.numHarmonics = numH;
        for (int h = 1; h <= numH; ++h)
        {
            float freq = (float)h * F0;
            if (freq > 8000.0f) break;
            auto L = [](float f, float Fc, float BW) {
                float r = BW * 0.5f;
                return (r * r) / ((f-Fc)*(f-Fc) + r*r);
            };
            float w = L(freq,F1,BW1)
                    + 0.8f * L(freq,F2,BW2)
                    + 0.5f * L(freq,F3,BW3)
                    + 0.25f * L(freq,F4,150.0f);
            w *= std::pow((float)h, -0.7f);
            fs.amplitudes[h-1] = w;
            fs.phases[h-1] = 0.0f;
        }
    }
    return spec;
}
```

**Critical perceptual waypoints:**
- The /a/→/e/ transition: F2 leaps from 1090→1840 Hz — this is the most dramatic move in the sweep. The sound "brightens" strongly.
- The /e/→/i/ transition: F1 drops from 530→270 Hz, F2 climbs to 2290 Hz — the vowel "tightens" dramatically.
- The /i/→/o/ transition: F2 collapses from 2290→840 Hz — the most dramatic single shift in the entire sweep, should be audible as a large timbral change.
- The /o/→/u/ transition: mostly F1 drops (570→300) with F2 stable — subtle rounding.

---

#### Whisper — formant-shaped noise (16 frames)

Whispered speech uses the same vocal tract formants as voiced speech but the source is turbulent airflow (broadband noise) instead of glottal pulses. This means:
1. **All harmonics get randomized phases** — simulates noisy/aperiodic source.
2. **Formant amplitudes are the same** as for /i/ across the sweep (whispered vowels tend to use "neutral to /i/" positioning), with a slight brightening from frame 0→15.
3. **More energy in high harmonics** than voiced vowels — spectral slope is shallower (slope ≈ -0.3 to 0.0 vs -0.7 for voiced).
4. **No fundamental reinforcement** — treat all harmonics equally (no spectral slope boost at h=1).

```cpp
static WavetableSpec makeWhisperSpec()
{
    // Whisper: formant-shaped noise. Uses /i/-like formant positions throughout
    // (whisper naturally produces a more neutral/bright tract shape).
    // Frame 0 = /u/-like dark whisper, Frame 15 = /i/-like bright whisper
    
    constexpr float F0 = 220.0f;
    constexpr int   numH = 96;  // more harmonics = more noise bandwidth
    
    struct WhisperFrame { float F1, F2, F3, BW1, BW2, BW3; };
    const WhisperFrame start = { 300,  870, 2240, 80,  100, 140 }; // /u/ dark
    const WhisperFrame end   = { 270, 2290, 3010, 60,   80, 100 }; // /i/ bright
    
    WavetableSpec spec;
    std::mt19937 rng(0xDEADBEEF);  // fixed seed for deterministic noise shape
    std::uniform_real_distribution<float> phaseDist(-3.14159f, 3.14159f);
    
    for (int frame = 0; frame < WavetableSpec::kNumFrames; ++frame)
    {
        float t = (float)frame / (float)(WavetableSpec::kNumFrames - 1);
        auto interp = [&](float a, float b) { return a + t * (b - a); };
        float F1  = interp(start.F1,  end.F1);
        float F2  = interp(start.F2,  end.F2);
        float F3  = interp(start.F3,  end.F3);
        float BW1 = interp(start.BW1, end.BW1);
        float BW2 = interp(start.BW2, end.BW2);
        float BW3 = interp(start.BW3, end.BW3);
        
        FrameSpec& fs = spec.frames[frame];
        fs.numHarmonics = numH;
        for (int h = 1; h <= numH; ++h)
        {
            float freq = (float)h * F0;
            if (freq > 10000.0f) break;
            auto L = [](float f, float Fc, float BW) {
                float r = BW * 0.5f;
                return (r * r) / ((f-Fc)*(f-Fc) + r*r);
            };
            float w = L(freq,F1,BW1)
                    + 0.8f * L(freq,F2,BW2)
                    + 0.6f * L(freq,F3,BW3);
            w *= std::pow((float)h, -0.3f);  // shallow slope = bright noisy sound
            fs.amplitudes[h-1] = w;
            fs.phases[h-1] = phaseDist(rng);  // KEY: random phases = incoherent = noise-like
        }
    }
    return spec;
}
```

**Why randomized phases make it noisy:** When all harmonics have identical phase (0), they sum constructively at t=0 producing a strong attack transient. With random phases, they add incoherently and the time-domain waveform resembles bandpass noise. This is the mechanism behind breathy/whispered vowels.

---

## Part B — Metallic Partials

### Physical background: why metal bars are inharmonic

A vibrating string's resonant modes follow the ratio 1:2:3:4:... because the restoring force is purely tensile (proportional to displacement). A vibrating bar's restoring force is bending stiffness (proportional to the second derivative of curvature), which leads to the Euler-Bernoulli beam equation. The modal frequencies scale as the **square** of the mode shape spatial frequency, producing inharmonic ratios.

For a free-free bar (no boundary constraints at either end — like a vibraphone bar, marimba bar, or railroad spike suspended at nodes):
```
fn/f1 = [ (2n+1) / 3 ]^2  (approximate, for modes n=1,2,3,...)
```

The experimentally measured ratios (PSU Acoustics lab, Gibson/UConn) are:

| Mode | (2n+1) value | Ratio to Mode 1 | Measured | Closest integer |
|---|---|---|---|---|
| 1st (fundamental) | 3 | 1.000 | 1.000 | 1 |
| 2nd overtone | 5 | 2.778 | 2.756 | 3 |
| 3rd overtone | 7 | 5.444 | 5.404 | 5 |
| 4th overtone | 9 | 9.000 | 8.933 | 9 |
| 5th overtone | 11 | 13.444 | 13.340 | 13 |

The small discrepancies between theory and measurement arise because real bars have non-uniform cross-sections (the vibraphone's arch undercut, for example, deliberately shifts mode 2 to a 2-octave ratio of 4.000).

### Inharmonic ratio reference table

| Wavetable | Partial 1 | Partial 2 | Partial 3 | Partial 4 | Partial 5 | Source |
|---|---|---|---|---|---|---|
| **BowedMetal** (vibraphone) | 1.000 | 4.000 | 10.000 | — | — | Tuned bars: 2-octave + octave+M3 |
| **GlassHarmonics** (glass harmonica) | 1.000 | 2.756 | 5.404 | 8.933 | — | Free-free bowl modes (2,0)+(3,0)+(4,0) |
| **Railroad** (free-free steel bar) | 1.000 | 2.756 | 5.404 | 8.933 | 13.340 | Euler-Bernoulli free-free beam |

**Bell partial reference (not a wavetable but useful for cross-reference):**

| Partial name | Ratio to prime | Interval |
|---|---|---|
| Hum | 0.500 | Octave below |
| Prime (strike note) | 1.000 | Reference |
| Tierce | 1.200 | Minor third above |
| Quint | 1.500 | Perfect fifth above |
| Nominal | 2.000 | Octave above |
| Deciem | 2.500 | Major third above nominal |
| Upper fifth | 3.000 | Twelfth above prime |
| Upper octave | 4.000 | Double octave above prime |

### Decay characteristics

Decay order (fastest decaying partial listed first, longest sustaining last):

**BowedMetal (vibraphone, bowed):**
- Mode 3 (10.000×) decays fastest — very high frequency, radiation damps quickly
- Mode 1 (4.000×) decays moderately
- Fundamental (1.000×) sustains longest — bowing actively maintains it
- Bowing EMPHASIZES higher harmonics vs mallet-struck (Wikipedia Vibraphone, confirmed)
- Frame sweep: frame 0 = sustained bowing (fundamental + 4.000 prominent), frame 15 = fading tail (only fundamental remaining)

**GlassHarmonics (wine glass / glass harmonica friction):**
- Near-harmonic — the (2,0) mode and its harmonics dominate
- Stick-slip friction excites mostly the (2,0) mode and its near-octave relationships
- Upper partials (5.404×, 8.933×) are very weak — glass transmits energy mainly in the lowest modes
- Sustained: all modes sustain similarly (glass has very low damping, high Q)
- Frame sweep: frame 0 = pure/clean (fundamental-heavy), frame 15 = slight "shimmer" (added 5.4× partial)

**Railroad (long steel bar, struck):**
- Mode 5 (13.34×) decays first — high frequency, short wavelength, fast radiation
- Mode 4 (8.933×) decays next
- Mode 3 (5.404×) — moderate
- Mode 2 (2.756×) — slow
- Mode 1 (fundamental) — very slow (minutes in a real rail; we simulate a few seconds of decay)
- Frame sweep: frame 0 = struck transient (all partials present, bell-like attack), frame 15 = pure ring (only fundamental + 2.756× remaining)

### Integer approximations (Option A — for current FrameSpec architecture)

Since FrameSpec only supports integer harmonic indices (h = 1, 2, 3, ..., 256), we must approximate non-integer partial ratios to the nearest integer. The perceptual impact is that we lose the beating/inharmonicity character but retain spectral energy in the right general region.

| Real ratio | Closest integer h | Error (cents) | Perceptual fidelity | Notes |
|---|---|---|---|---|
| 1.000 | 1 | 0 | Perfect | — |
| 2.756 | 3 | -537 cents (≈5.4 semitones flat) | Fair | Sounds like a fifth above, not quite 2.756× |
| 4.000 | 4 | 0 | Perfect | Vibraphone is deliberately tuned here |
| 5.404 | 5 | -140 cents (≈1.4 semitones flat) | Good | Close enough to a major third + octave |
| 8.933 | 9 | +117 cents | Good | Close to 3× octave above third harmonic |
| 10.000 | 10 | 0 | Perfect | Vibraphone's third mode is a deliberate 10× |
| 13.340 | 13 | -45 cents | Excellent | Very close, perceptually fused |

**Option C potential (future):** For the 2.756× ratio specifically, using h=2 and h=3 together with a slight amplitude tilt creates perceivable beating that sounds inharmonic. This is worth implementing in Phase 10c when PartialSpec is available.

**Recommendation:** Use Option A for now but document that `h=3` for `2.756×` is the worst approximation — 537 cents error means it sounds like a perfect fifth instead of an augmented fifth. The railroad in particular will be less authentic until PartialSpec lands.

### Integer approximation mapping for each wavetable

**BowedMetal:** Uses vibraphone-tuned ratios (1.000, 4.000, 10.000) — all map perfectly to integers h=1, 4, 10. This is the most faithful to Option A because vibraphone bars are deliberately machined to these integer-octave relationships.

| Real partial | Integer h | Error |
|---|---|---|
| 1.000× | h=1 | 0 cents |
| 4.000× | h=4 | 0 cents |
| 10.000× | h=10 | 0 cents |

**GlassHarmonics:** Uses free-free beam ratios, but glass is close to harmonic in its (2,0) mode family.

| Real partial | Integer h | Error |
|---|---|---|
| 1.000× | h=1 | 0 cents |
| 2.756× | h=3 | −537 cents |
| 5.404× | h=5 | −140 cents |
| 8.933× | h=9 | +117 cents |

**Railroad:** Full free-free beam series.

| Real partial | Integer h | Error |
|---|---|---|
| 1.000× | h=1 | 0 cents |
| 2.756× | h=3 | −537 cents |
| 5.404× | h=5 | −140 cents |
| 8.933× | h=9 | +117 cents |
| 13.340× | h=13 | −45 cents |

### Implementation spec for each metallic wavetable

#### BowedMetal — bowed vibraphone bar

WT POS sweep: frame 0 = actively bowed (rich, sustained, all 3 modes), frame 15 = bow lifted (fundamental only, gentle decay tail).

```cpp
static WavetableSpec makeBowedMetalSpec()
{
    // Vibraphone: tuned partial ratios h=1, h=4, h=10
    // Bowed character: emphasizes upper harmonics (glassy tone)
    // Decay across frames: h=10 fades first, h=4 fades next, h=1 stays
    
    WavetableSpec spec;
    for (int frame = 0; frame < WavetableSpec::kNumFrames; ++frame)
    {
        float t = (float)frame / (float)(WavetableSpec::kNumFrames - 1); // 0=bowing, 1=fading
        
        // Amplitude envelopes for each partial across frame sweep
        float amp1  = 1.0f;                          // fundamental always present
        float amp4  = 0.7f * (1.0f - 0.6f * t);    // 4th harmonic fades
        float amp10 = 0.5f * (1.0f - 0.9f * t);    // 10th harmonic fades fastest
        
        // Add mild noise around each partial to simulate bow friction
        // (done via slight phase randomization at higher harmonics)
        // Adding small amplitudes at h=2, h=3, h=5, h=6 for bow rosin noise:
        float bowNoise = 0.15f * (1.0f - t);  // only during active bowing
        
        FrameSpec& fs = spec.frames[frame];
        fs.numHarmonics = 12;
        
        // Main bowed partials
        fs.amplitudes[1-1]  = amp1;
        fs.amplitudes[4-1]  = amp4;
        fs.amplitudes[10-1] = amp10;
        
        // Bow-noise harmonics (subtle, add "rosin" texture)
        fs.amplitudes[2-1] = bowNoise * 0.4f;
        fs.amplitudes[3-1] = bowNoise * 0.3f;
        fs.amplitudes[5-1] = bowNoise * 0.2f;
        fs.amplitudes[6-1] = bowNoise * 0.1f;
        
        // Small amount of phase randomness for bow rosin noise
        // (zero for main partials — keep them phase-coherent for pitch clarity)
        fs.phases[1-1] = 0.0f;
        fs.phases[4-1] = 0.0f;
        fs.phases[10-1] = 0.0f;
    }
    return spec;
}
```

**Key authentication:** The vibraphone h=1:4:10 mapping is exact, not approximated. This wavetable will be the most accurate of the three metallic tables. The bow noise harmonics (h=2,3,5,6) are intentionally low amplitude — they add "rosin texture" without masking the clean bell-like character. The sweep from "actively bowed" to "decay tail" means users can use WT POS as a bow-lift gesture.

---

#### GlassHarmonics — glass harmonica (friction on glass bowls)

Character: pure, ethereal, almost sine-like at low harmonic content, with higher modes adding shimmer. Franklin's glass harmonica produces sustained tones through stick-slip friction, primarily in the (2,0) vibrational mode. The sound is nearly pure but with characteristic high-frequency shimmer.

```cpp
static WavetableSpec makeGlassHarmonicsSpec()
{
    // Glass harmonica: near-harmonic but slightly inharmonic
    // Dominant mode: (2,0) = fundamental, with harmonics approximated to integers
    // Real ratios: 1.000, 2.756, 5.404, 8.933
    // Integer approx: h=1, h=3, h=5, h=9
    //
    // WT POS sweep: frame 0 = pure/clean (fundamental only),
    //               frame 15 = shimmering (all partials contributing)
    // This represents adding more "bow pressure" on the glass.
    
    WavetableSpec spec;
    for (int frame = 0; frame < WavetableSpec::kNumFrames; ++frame)
    {
        float t = (float)frame / (float)(WavetableSpec::kNumFrames - 1);
        
        // Glass has very low damping — all partials sustain together once excited
        // The sweep instead represents spectral richness (low WT POS = sine-like purity)
        float amp1 = 1.0f;
        float amp3 = 0.15f * t;      // 2.756× approx → h=3, grows with WT POS
        float amp5 = 0.08f * t;      // 5.404× approx → h=5
        float amp9 = 0.04f * t;      // 8.933× approx → h=9
        
        // Glass modes are very narrow (high Q) — they produce distinct overtone
        // "pings" rather than a continuous spectrum. Low amplitude is correct.
        
        FrameSpec& fs = spec.frames[frame];
        fs.numHarmonics = 10;
        fs.amplitudes[1-1] = amp1;
        fs.amplitudes[3-1] = amp3;
        fs.amplitudes[5-1] = amp5;
        fs.amplitudes[9-1] = amp9;
        
        // All zero phases — glass produces very pure, coherent tones
        // (no rosin noise, no stick-slip randomness in the fundamental mode)
    }
    return spec;
}
```

**Note on inharmonicity loss:** The h=3 approximation for 2.756× loses 537 cents. In practice this means the second partial sounds like a perfect fifth (harmonic) rather than the characteristic "slightly flat perfect fifth" of glass. For Phase 10c with PartialSpec, the ideal implementation places energy at ratio 2.756 directly, creating a subtle beating tension with the h=2 integer harmonic that is the hallmark of glass sound.

---

#### Railroad — distant railroad spike / long steel bar

Character: struck iron/steel produces a "clang" with a complex inharmonic transient that decays into a pure ring. The frame sweep represents the temporal evolution of this decay — frame 0 is the "just struck" moment with all modes screaming, frame 15 is minutes later with only the fundamental remaining.

```cpp
static WavetableSpec makeRailroadSpec()
{
    // Free-free steel bar (like a railroad rail or spike)
    // Real partial ratios: 1.000, 2.756, 5.404, 8.933, 13.340
    // Integer approx: h=1, h=3, h=5, h=9, h=13
    //
    // WT POS sweep represents temporal decay after impact:
    //   Frame 0  = initial strike (all partials at full strength, metallic clang)
    //   Frame 8  = mid-decay (h=13 and h=9 faded significantly)
    //   Frame 15 = late decay (only h=1 and slight h=3 remain, pure ring)
    //
    // Spectral evolution: high partials decay first (radiation damping ∝ f^4)
    
    WavetableSpec spec;
    for (int frame = 0; frame < WavetableSpec::kNumFrames; ++frame)
    {
        float t = (float)frame / (float)(WavetableSpec::kNumFrames - 1);
        
        // Exponential-style decay curves per partial (higher = faster)
        // t=0 = struck, t=1 = ringing pure
        float amp1  = 1.0f;                              // fundamental: always 1.0
        float amp3  = 0.60f * std::pow(1.0f-t, 1.5f);  // 2.756×: slow decay
        float amp5  = 0.40f * std::pow(1.0f-t, 2.5f);  // 5.404×: medium decay
        float amp9  = 0.25f * std::pow(1.0f-t, 4.0f);  // 8.933×: fast decay
        float amp13 = 0.15f * std::pow(1.0f-t, 6.0f);  // 13.34×: very fast decay
        
        // On initial strike (frame 0): add inharmonic "impact noise" at h=2, h=4, h=6
        // These represent the wideband transient that's NOT part of the modal ring.
        float impactNoise = 0.20f * (1.0f - t) * (1.0f - t);
        
        FrameSpec& fs = spec.frames[frame];
        fs.numHarmonics = 14;
        
        // Main modal partials
        fs.amplitudes[1-1]  = amp1;
        fs.amplitudes[3-1]  = amp3;
        fs.amplitudes[5-1]  = amp5;
        fs.amplitudes[9-1]  = amp9;
        fs.amplitudes[13-1] = amp13;
        
        // Impact transient fill (decays quickly with frame position)
        fs.amplitudes[2-1] = impactNoise * 0.5f;
        fs.amplitudes[4-1] = impactNoise * 0.4f;
        fs.amplitudes[6-1] = impactNoise * 0.3f;
        fs.amplitudes[7-1] = impactNoise * 0.2f;
        
        // Slightly randomized phases for impact frames (strike is noisy)
        // Pure phases for late frames (ring is coherent)
        float phaseNoise = impactNoise * 1.0f;  // 0 at frame 15, ~0.2 at frame 0
        fs.phases[2-1] = phaseNoise * 0.5f;
        fs.phases[4-1] = phaseNoise * 0.8f;
        fs.phases[6-1] = phaseNoise * 1.2f;
    }
    return spec;
}
```

**Key authentication:** The temporal-decay-as-WT-POS-sweep is acoustically honest — this is exactly what happens to a real struck metal bar. At frame 0, the sound is complex and bell-like (inharmonic overtone chaos). At frame 15, it is a near-pure sine. Users who sweep WT POS from left to right while holding a note hear the simulated decay. Combined with WARP (which could add spectral stretch), this becomes very expressive.

---

## Summary of integer approximation quality

For tonight's Phase 11l implementation, the approximation fidelity ranking is:

1. **BowedMetal** — BEST: all three vibraphone partials (1.0, 4.0, 10.0) map exactly to h=1, h=4, h=10. Zero approximation error. Will sound genuinely metallic and bell-like.

2. **Railroad** — GOOD overall: h=1 (exact), h=13 for 13.34× (45 cents — nearly perfect), h=5 for 5.404× (140 cents — acceptable), h=9 for 8.933× (117 cents — acceptable). The weak link is h=3 for 2.756× (537 cents — a whole fifth off). The temporal decay sweep compensates perceptually because the h=3 partial is most prominent only in early frames.

3. **GlassHarmonics** — ACCEPTABLE: same 2.756× → h=3 problem, but glass sound is so close to pure that the higher partials are very quiet (amp ≤ 0.15). The fundamental dominates. The inharmonicity is mostly in character, not pitch perception.

---

## Future: PartialSpec migration notes

When Phase 10c adds PartialSpec support for arbitrary frequency ratios, replace integer approximations with exact ratios:

```cpp
// Future PartialSpec (Phase 10c)
struct Partial { float freqRatio; float amplitude; float phase; };

// BowedMetal exact (already perfect with integers — no change needed)
// GlassHarmonics exact
Partial glassPartials[] = {
    { 1.000f, 1.00f, 0.0f },
    { 2.756f, 0.15f, 0.0f },  // Was h=3, now exact 2.756×
    { 5.404f, 0.08f, 0.0f },  // Was h=5, now exact 5.404×
    { 8.933f, 0.04f, 0.0f },  // Was h=9, now exact 8.933×
};
// Railroad exact (same ratio set as glass but more partials and higher amplitudes)
Partial railPartials[] = {
    {  1.000f, 1.00f, 0.0f },
    {  2.756f, 0.60f, 0.0f },  // Was h=3
    {  5.404f, 0.40f, 0.0f },  // Was h=5
    {  8.933f, 0.25f, 0.0f },  // Was h=9
    { 13.340f, 0.15f, 0.0f },  // Was h=13 (closest approximation was good anyway)
};
```

The `h=3` for `2.756×` approximation is the single highest-priority fix when PartialSpec lands. It currently sounds like a perfect fifth chord (consonant) rather than the slightly-flat-fifth beating characteristic of real metal bars (which gives the "metallic unease"). Fixing this one approximation will dramatically improve both GlassHarmonics and Railroad authenticity.
