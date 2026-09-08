# RESEARCH STREAM 1 — CODE MINING (agent complete, verbatim notes)

# PHYSICAL MODELING CODE-MINING NOTES (all source files read directly; local clones at scratchpad/pm/{stk,eurorack,stmlib,DaisySP,faustlibraries,csound,surge})

## LICENSE SUMMARY (port-safety)
- **STK** (thestk/stk): MIT-style permissive, confirmed verbatim in `stk/LICENSE`. Non-binding request to send mods upstream. **PORT-SAFE.** (Waveguide patents Stanford/Yamaha filed ~1986-1994, expired. Non-issue in 2026.)
- **Mutable Instruments eurorack** (pichenettes/eurorack) + **stmlib**: MIT, confirmed in every file header. **PORT-SAFE** (hardware/name is CC-BY-SA; DSP code is MIT).
- **Plaits**: same repo/license. **PORT-SAFE.**
- **DaisySP** (electro-smith/DaisySP): MIT, confirmed `DaisySP/LICENSE`. **PORT-SAFE.**
- **faustlibraries** (grame-cncm/faustlibraries): physmodels.lib = Michon/GRAME, MIT-style. **PORT-SAFE (formulas trivially re-derivable anyway).**
- **Csound** (csound/csound): **LGPL-2.1**. **CONCEPTS/CONSTANTS ONLY, do not copy code.** (Parameter ranges/magic numbers are facts, not copyrightable expression — safe.)
- **Surge XT**: **GPL-3.0**. **CONCEPTS ONLY.**
- VCV AudibleInstruments = GPL-3 port of MI (port from MIT originals instead). mi-UGens GPL-3. jatinchowdhury18 PM work GPL-3, skip.

---

## 1. STK (files: src/Plucked.cpp, Twang.cpp, StifKarp.cpp, Bowed.cpp, BowTable.h, Flute.cpp, JetTable.h, Clarinet.cpp, ReedTable.h, Brass.cpp, Modal.cpp, ModalBar.cpp, BandedWG.cpp, Mandolin.cpp, Guitar.cpp, Saxofony.h/.cpp, DelayA.cpp, BiQuad.cpp)

### 1.1 Plucked (basic KS)
Flow: noise burst → OnePole pickFilter → DelayA loop → OneZero loopFilter.
- `delay = SR/f − loopFilter.phaseDelay(f)`; `loopGain = 0.995 + f·5e-6`, cap 0.99999 (freq-dependent gain compensates HF loop rolloff — KEY TRICK in every STK string).
- pluck: `pickFilter.setPole(0.999 − amp·0.15); gain = amp·0.5`; fill delay for whole length with `0.6·lastOut + pickFilter(noise)` (additive re-pluck keeps ringing energy).
- noteOff: `loopGain = 1 − amp`.
Verdict: superseded by Twang.

### 1.2 Twang (Jaffe-Smith enhanced KS; THE building block)
```
lastOut = delayA.tick( input + FIR_loop(delayA.lastOut) );
lastOut -= combDelay.tick(lastOut);   // pluck-position comb ON OUTPUT
lastOut *= 0.5;
```
- loop FIR default b=[0.5,0.5]; gain = loopGain + f·5e-6 cap 0.99999; `delay = SR/f − fir.phaseDelay(f)`; comb delay = `0.5·pluckPosition·delay`.
Verdict: **PORT.** Cleanest core for plucked/struck string with position comb + external excitation input (→ commuted synthesis AND feedback guitar).

### 1.3 StifKarp (stiff string / piano-ish)
Flow: noise pluck → delayA → [loopGain] → 4× cascade biquad ALLPASS "stretch" (stiffness) → back to delay; pickup comb on output.
- Stiffness allpass per stage: `t = 0.5 + stretch·0.5` (cap 0.99999); `a2=b0=t²`, `a1=b1=−2t·cos(2π·fs_i/SR)`; stage center freqs start `f·2`, step `dF=((SR/2)−2f)/4`. stretch ∈ 0.9..1.0 (`0.9+0.1(1−n)`).
- `delay = SR/f − 0.5`; loopGain = 0.995 + f·5e-6; damping CC: base = 0.97+0.03n.
- pluck fill: `0.6·last + 0.4·noise·amp`.
Verdict: port 4-allpass stretch as cheap "piano stiffness" option (vs Rings' single stretched allpass — different flavor).

### 1.4 Bowed (MSW violin) — COMPLETE VIOLIN
```
bowVel = maxVel·adsr;                       maxVel = 0.03 + 0.2·amp
bridgeRefl = −OnePole_string(bridgeDelay.last)    // pole = 0.75 − 0.2·(22050/SR), gain 0.95
nutRefl    = −neckDelay.last
deltaV = bowVel − (bridgeRefl + nutRefl)
newV = bowDown ? deltaV · bowTable(deltaV) : 0
neckDelay.tick(bridgeRefl + newV);  bridgeDelay.tick(nutRefl + newV)
out = 0.1248 · SOS6(SOS5(...SOS1(bridgeDelay.last)))
```
- **BowTable (Smith 1986 friction):** `y = clamp( ( |(v+offset)·slope| + 0.75 )^−4 , 0.01..0.98 )`. slope default 3.0; **bow pressure → slope = 5 − 4·pressure** (harder press = wider stick region). offset 0.001.
- Delays: `base = SR/f − 4`; bridge = base·β, neck = base·(1−β), **β default 0.127236** (bow position).
- Vibrato 6.12723 Hz on NECK delay only: `neck = base(1−β) + base·vibGain·sin` (gain ≤ 0.4·n).
- ADSR 0.02/0.005/0.9/0.01. noteOn rate = amp·0.001; noteOff release (1−amp)·0.005.
- **Violin body = 6 cascaded biquads (Esteban Maestre measured), (b0,b1,b2,a1,a2):**
```
(1, 1.5667, 0.3133, −0.5509, −0.3925)
(1, −1.9537, 0.9542, −1.6357, 0.8697)
(1, −1.6683, 0.8852, −1.7674, 0.8735)
(1, −1.8585, 0.9653, −1.8498, 0.9516)
(1, −1.9299, 0.9621, −1.9354, 0.9590)
(1, −1.9800, 0.9888, −1.9867, 0.9923)
```
(near 22.05/44.1k; output gain 0.1248.)
Verdict: **PORT whole thing.**

### 1.5 Flute
```
breath = maxPress·adsr;  breath += breath·(0.15·noise + 0.05·vib(5.925Hz))
refl = −OnePole(boreDelay.last)        // pole = 0.7 − 0.1·(22050/SR)
pd = breath − jetRefl·refl             // jetRefl = 0.5
pd = jetDelay.tick(pd)
pd = DCblock( jet(pd) ) + endRefl·refl // endRefl = 0.5 ; DC blocker AFTER jet (2020 fix)
out = 0.3 · boreDelay.tick(pd) · outputGain
```
- **Jet nonlinearity: `y = clamp(x·(x²−1), ±1)`** (cubic sigmoid).
- Tuning: `f_loop = 0.66666·f` (overblown); `delay = SR/f_loop − phaseDelay − 1`; jetDelay = delay·jetRatio, **jetRatio default 0.32** (0.08+0.48n = "mouth position"; ~0.5 jumps octave).
- **Breath window: noteOn → `startBlowing(1.1 + 0.20·amp)`, `maxPressure = amplitude/0.8`** ⇒ pressure ~1.38–1.63 (>1 required to speak); adsr 0.005/0.01/0.8/0.01.
Verdict: **PORT.**

### 1.6 Clarinet — 15 lines for a working clarinet
```
breath = env;  breath += breath·0.2·noise + breath·0.1·vib(5.735Hz)
pd = −0.95·OneZero(delay.last)        // commuted loss
pd = pd − breath
out = delay.tick( breath + pd·reed(pd) )
```
- **ReedTable: `y = clamp(offset + slope·pd, −1, +1)`; offset=0.7, slope=−0.3; stiffness → slope = −0.44 + 0.26·n.** (y=+1 ⇒ reed slams shut.)
- `delay = 0.5·SR/f − phaseDelay(f) − 1` (HALF period, closed-open bore).
- **Breath window: noteOn → `0.55 + 0.30·amp`** (speaks ≈0.55–0.85; threshold ~0.5).
Verdict: **PORT.**

### 1.7 Brass
```
breath = maxPress·adsr + vibGain·vib(6.137Hz)
mouth = 0.3·breath;  bore = 0.85·delay.last
dp = lipFilter( mouth − bore )     // BiQuad setResonance(f_lip, r=0.997, normalized)
dp = min(dp², 1.0)                 // "position → area", saturating
out = dp·mouth + (1−dp)·bore       // scattering
out = delay.tick( DCblock(out) )
```
- `slideTarget = 2·SR/f + 3` samples; **lip resonance: `setResonance(f, 0.997)`; lip tension → `f_lip = f·4^(2n−1)`** (±2 octaves = harmonic selection!). Slide length: delay = slideTarget·(0.5+n).
- BiQuad setResonance (normalized): `a2=r², a1=−2r·cos(2πf/SR); b0=0.5−0.5a2, b1=0, b2=−b0`.
- Lip filter gain 0.03; ADSR 0.005/0.001/1.0/0.010.
Verdict: **PORT** — brass character = lip-tension↔harmonic-selection interplay; ~12 lines + biquad.

### 1.8 Modal + ModalBar (struck bars)
Core: excitation wavetable → Envelope → **OnePole "strike hardness color" (pole = 1 − amp on strike!)** → N=4 BiQuad resonators summed → direct-mix → optional AM vibrato (vibraphone).
- `setRatioAndRadius`: negative ratio = **absolute Hz fixed mode** (doesn't track pitch); positive scales with f0; auto-halve until below Nyquist.
- Strike-position → first 3 mode gains: `g0=0.12·sin(π·p)`, `g1=−0.03·sin(0.05+3.9π·p)`, `g2=0.11·sin(−0.05+11π·p)`.
- Stick hardness h: excitation playback rate `0.25·4^h`, masterGain `0.1+1.8h`.
- noteOff damp: all radii ×= (1 − amp·0.03).
- **PRESET TABLE (ratios / radii / gains / [hardness, position, directGain]):**
```
Marimba:    {1.0, 3.99, 10.65, −2443Hz} {0.9996,0.9994,0.9994,0.999} {0.04,0.01,0.01,0.008} {0.4297,0.4453,0.0938}
Vibraphone: {1.0, 2.01, 3.9, 14.37}     {0.99995,0.99991,0.99992,0.9999} {0.025,0.015,0.015,0.015} {0.3906,0.5703,0.0781} + AM vib 0.2
Agogo:      {1.0, 4.08, 6.669, −3725Hz} {0.999×4} {0.06,0.05,0.03,0.02} {0.6094,0.3594,0.1406}
Wood1:      {1.0, 2.777, 7.378, 15.377} {0.996,0.994,0.994,0.99} {0.04,0.01,0.01,0.008} {0.4609,0.375,0.0469}
Reso:       {1.0, 2.777, 7.378, 15.377} {0.99996,0.99994,0.99994,0.9999} {0.02,0.005,0.005,0.004} {0.4531,0.25,0.1016}
Wood2:      {1.0, 1.777, 2.378, 3.377}  {0.996,0.994,0.994,0.99} {0.04,0.01,0.01,0.008} {0.3125,0.4453,0.1094}
Beats:      {1.0, 1.004, 1.013, 2.377}  {0.9999×3,0.999} {0.02,0.005,0.005,0.004} {0.3984,0.2969,0.0703}
2Fix:       {1.0, 4.0, −1320Hz, −3960Hz}{0.9996,0.999,0.9994,0.999} {0.04,0.01,0.01,0.008} {0.4531,0.4531,0.0703}
Clump:      {1.0, 1.217, 1.475, 1.729}  {0.999×4} {0.03×4} {0.3906,0.5703,0.0781}
```
Verdict: **PORT preset table + strike-position sine law + hardness law** (replace wavetable excitation with filtered click/noise pulse — see Elements mallet).

### 1.9 BandedWG (banded waveguides: bowed/struck bars, glass, bowls)
Per mode i: delay (length = SR/f/ratio_i) + peak-normalized biquad bandpass at f·ratio_i, radius = `1 − 32π/SR` (fixed ~32Hz bandwidth). Loop: `bp_i.tick(input + gain_i·delay_i.last); delay_i.tick(bp_i.last)`; out = Σ bp_i ·4. Freq cap ≤1568 Hz.
- Bow drive: `velInput = Σ baseGain·delay_k.last`; `input = (bowVel − velInput)·bowTable(Δv)/nModes`; bowtable slope = `10 − 9·pressure`; maxVel = `0.03+0.1·amp`; velocity-tracking mode: `bowVel = 0.9995·bowVel + bowTarget; bowTarget ·= 0.995`.
- Pluck: fill each mode delay with `excitation_i·amp/nModes`.
- **MODE TABLES:**
```
Uniform Bar:  {1.0, 2.756, 5.404, 8.933}          gains 0.9^(i+1)
Tuned Bar:    {1.0, 4.0198391420, 10.7184986595, 18.0697050938}  gains 0.999^(i+1)
Glass Harm.:  {1.0, 2.32, 4.25, 6.63, 9.38}       gains 0.999^(i+1)
Tibetan Bowl (12): {0.996108344,1.0038916562, 2.979178,2.99329767, 5.704452,5.704452, 8.9982,9.01549726, 12.83303,12.807382, 17.2808219,21.97602739726}
  basegains {0.999925960128219×2, 0.999982774366897×2, 1.0×2, 1.0×2, 0.999965497558225×2, ~1.0, ~1.0}
  excitations {1.19,1.19, 1.0915,1.0915, 4.2995,4.2995, 4.0063,4.0063, 0.7063,0.7063, 5.7063,5.7063}
```
(bowl = beating mode PAIRS ~0.1-0.8% apart — THE singing-bowl shimmer recipe.)
Verdict: **PORT** — only bowed-bar/bowl model in the survey; detuned-pairs trick is gold.

### 1.10 Mandolin (commuted synthesis)
Two Twang strings, detune 0.995 (`1 − 0.1n`); **body = 12 recorded body-IR samples played INTO both strings on pluck**; body size = rate scaling (`rate = size·22050/SR`); mic position = which IR.
Verdict: port ARCHITECTURE (excite string with body IR = zero-cost body resonance); ship own short body IRs or synthetic decaying-resonator burst.

### 1.11 Guitar (N coupled strings + feedback)
- Bridge coupling: `coupling = couplingGain(0.001 base, up to 1.5·BASE·n) · OnePole(pole 0.9)(lastTotalOut) / nStrings` added to EVERY string input → sympathetic coupling & feedback (external input supported).
- String auto-sleep: |out| < 0.001 for 0.1s → off (CPU).
Verdict: port coupling one-liner + auto-sleep watchdog (healers mandate).

### 1.12 Saxofony (conical/"blown string" reed variant)
Two delays around reed junction: `pd = breath − (−0.95·filter(d0.last) − d1.last)`; `d1.tick(temp); d0.tick(breath − pd·reed(pd) − temp)`. Reed offset 0.7, slope **+0.3** (positive, vs clarinet −0.3); blow position splits the two delays.
Verdict: optional 3rd reed flavor; cheap.

### 1.13 DelayA (allpass-interpolated delay; tuning-critical)
- `setDelay`: fractional alpha kept in **[0.5, 1.5]**; `coeff = (1−alpha)/(1+alpha)`.
- `tick`: `out = −coeff·lastOut + apInput + coeff·buf[outPoint]`. Near-unity gain at all freqs → loop gain exact; use for MAIN pitch delay; linear interp detunes/damps highs.
Verdict: **PORT** (or Rings' Hermite read; DelayA cheaper and exactly gain-1).

---

## 2. MUTABLE INSTRUMENTS RINGS (resonator.cc, string.cc, part.cc, plucker.h, lookup_tables.py, stmlib/filter.h)
SR = 48000, block 24, a3 = 440/SR. Frequencies normalized f = Hz/SR.

### 2.1 MODAL RESONATOR — 64 SVF bandpass modes
```
stiffness = lut_stiffness[structure]
q = 500 · 10^(4·damping)                             // Q 500 → 5e6
brightness_att = (1−structure)^8;  brightness' = brightness·(1 − 0.2·brightness_att)
q_loss = brightness'·(2−brightness')·0.85 + 0.15     // per-mode Q multiplier (≤1)
q_loss_recovery = structure·(2−structure)·0.1
for i in 0..63:
    f_i = f0·(i+1)·stretch_factor_i   (stretch_factor += stiffness each mode;
        stiffness *= 0.93 if stiffness<0 else *= 0.98)      // walk, not constant!
    clamp f_i ≤ 0.49, stop beyond
    SVF_i.set_f_q(f_i, 1 + f_i·q)
    q *= q_loss;  q_loss += q_loss_recovery·(1−q_loss)  // brightness = spectral tilt of decay time
```
Process: `input·0.125` → mode SVF bandpass → **amplitude of mode i = 0.5 + 0.5·cos(2π·(i+1)·position)** (position comb in FREQUENCY domain — no flange when modulated). Odd modes → out, even → aux (stereo width). Polyphony: `64/polyphony − 4` modes per voice.

### 2.2 STRUCTURE→STIFFNESS LUT — inharmonicity macro law (THE most valuable mapping)
```
g < 0.25 : stiffness = −(0.25−g)·0.25        // −0.0625..0 : partials squeeze (membrane/plate)
0.25–0.3 : stiffness = 0                      // harmonic plateau (strings) — DEAD ZONE
0.3–0.9  : stiffness = 0.01·10^(((g−0.3)/0.6)·2.005) − 0.01   // 0..~1.0 exp (piano→bell)
g ≥ 0.9  : u=((g−0.9)/0.1)²; stiffness = 1.5 − cos(u·π)/2     // 1.0..2.0 (gong/bell)
last two entries forced 2.0
```
One scalar sweeps drum→string→piano→bell.

### 2.3 KS STRING (string.cc) — the reference KS
```
delay = 1/f0 (clamp 4..2044); upsampler below 11.7 Hz
position comb delay = delay · (0.5 − 0.98·|position−0.5|)
lf_damping = d·(2−d)
rt60_samples = 0.07 · 2^(8·lf_damping) · SR                   // 0.07s → ~18s T60
damping_coefficient = 2^( max(−120·delay/rt60, −127) /12 )    // per-trip gain: −60dB at RT60
brightness' = brightness²
noise_filter = 2^((brightness−1)·4)
damping_cutoff_semitones = min(24 + d²·48 + B²·24, 84)
damping_f = min(f0·2^(dc/12), 0.499);  IIR damping = SVF LP (q=0.5)
delay *= 1 − lut_svf_shift[damping_cutoff]                     // phase-delay compensation!
   (closed form: svf_shift = 2·atan(1/ratio)/2π, ratio = 2^(cutoff_semitones/12))
delay -= 1.0                                                   // FIR delay
damping ≥ 0.95: crossfade all coeffs to INFINITE SUSTAIN over 0.95..1.0
```
FIR damping: `y = k·(h0·x[n−1] + h1·(x[n]+x[n−2]))`, `h0=(1+B)/2, h1=(1−B)/4`, k = damping_coefficient.
Per sample:
```
DISPERSION (>0):
  stretch_point = disp·(2−disp)·0.475
  ap_gain = −0.618·disp/(0.15+|disp|)
  noise_amount = disp>0.75 ? (4(disp−0.75))²·0.025 : 0
  delay *= 1 + LP(noise)·noise_amount          // stochastic delay FM = piano shimmer/rattle
  s = ReadHermite(delay − ap_delay); s = stretch.Allpass(s, ap_delay=delay·stretch_point, ap_gain)
CURVED BRIDGE (<0):
  delay *= 1 − curved_bridge·(disp²·0.01)
  v=|s|−0.025; sign = s>0?1:−1.5; curved_bridge=(|v|+v)·sign   // asymmetric rectifier
  s crossfaded toward DC-blocked by |disp|  (sitar/biwa buzz)
s += in;  s = FIRdamp(s);  s = SVF_LP(s);  Write(s)
out = s;  aux = Read(comb_delay)       // consumer uses out−aux
```
- **Structure → dispersion bipolar (part.cc):** `disp = s<0.24 ? (s−0.24)·4.166 : s>0.26 ? (s−0.26)·1.35135 : 0` → [−1..+1], dead zone 0.24–0.26.

### 2.4 PLUCKER excitation (plucker.h) — ~20 lines, PORT
Noise burst of ONE period → **comb (Read at period·(position·0.9+0.05), feedback (1−position)·0.8)** → SVF LP, q=1. Pre-shapes burst like real pluck at position p.

### 2.5 PART / sympathetic strings (part.cc)
- Models: MODAL, SYMPATHETIC_STRING, STRING (dispersion), FM_VOICE, SYMPATHETIC_QUANTIZED, STRING_AND_REVERB. Output gains {1.4, 1.0, 1.4, 0.7, 1.0, 1.4} → limiter.
- Exciter filter: cutoff base `c = brightness·(2−brightness)`; internal: `fc = f0·2^((c−0.5)·96/12)`, q=1.5; external audio: `fc = 0.4·2^((c−1)·108/12)`, q=0.8. Modal internal strike = single pulse `0.25·2^(fc²·24/12)/fc`.
- **Sympathetic set**: intervals `[tonic, −12, −7.02, 0, +7.02, +12, +19.02, +24, +24]` walked by `structure·7` with `Squash(frac)` (snaps to integers with morph zones); half the strings detuned by `{0.013, 0.011, 0.007, 0.017}` semitones. Chord mode: 11-chord table.
- Sympathetic drive: main string's `0.2/n·(out−aux)`; damping pinned `0.7+0.27·d`, position wobbled by per-string LFOs ({0.5,0.4,0.35,0.23,0.211,0.2,0.171}·block/SR), pitch glide coeff `2^((b−1)·36/12)` (sluggish retune), per-string damping staircase `d + i/n·(0.95−d)`.
- Round-robin voice allocation + note-lag filter (1ms/10ms strum guard).
- **stmlib SVF (TPT/Simper)**: `g = tan(πf)` (poly approx tiers in file), `r = 1/Q`, `h = 1/(1+rg+g²)`; `hp=(in−(r+g)s1−s2)h; bp=g·hp+s1; s1=g·hp+bp; lp=g·bp+s2; s2=g·bp+lp`. BAND_PASS_NORMALIZED = bp·r. **PORT as the mode filter.**

---

## 3. ELEMENTS (exciter.cc, tube.cc, resonator.cc/.h, voice.cc, lookup_tables.py)

### 3.1 Exciter suite (bow/blow/strike)
Seven models: GRANULAR_SAMPLE_PLAYER, SAMPLE_PLAYER, MALLET, PLECTRUM, PARTICLES, FLOW, NOISE. Post-filtered by SVF LP, **timbre → cutoff = 32Hz·10^(2.7·timbre)** (32Hz→16kHz), r=2; NOISE: `r = 1/(0.5·10^(3·p))` (Q 0.5→500).
- **Mallet:** one impulse `out[0] = (0.42/f)·4^(f²)` — amplitude EXACTLY compensates LP so strike loudness constant across timbre. Release-gate raises damping (palm mute).
- **Plectrum:** rising edge → NEGATIVE pre-displacement `−amp·(0.05+signature·0.2)`, after `4096·p² + 64` samples the +amp impulse (string pushed then released!).
- **Particles:** multiplicative random-walk `s ∈ [0.02, range+0.25]`, ×/÷ `1.05+0.5·u²` prob 0.3/0.3, spacing `s·0.15·SR`, amp `s·pulseAmp·(1−(1−range)²)` → bouncing-bead strikes.
- **Flow (BOW):** random telegraph: flips when `u < 1e−4 + p⁴·0.125`; `out = state + (u−0.5−state)·p⁴scale` → sparse friction noise.
- **Granular breath (blow):** looped 8s noise wavetable, restart prob 1%/sample, pitch `2^((72·timbre−60)/12)`.
- Accent gain LUT `10^(1.5(x−0.5))`.

### 3.2 Tube (20-line clarinet, runs INSIDE exciter path)
```
delay = 1/f (halved until < 1024)
damping' = 3.6 − damping·1.8;  lpf_c = min(f·(1+timbre²·256), 0.995)
breath = in·damping' + 0.8
in_wave = lerp-read(delay)
pd = −0.95·(in_wave·envelope + zero_state) − breath;  zero_state = in_wave
reed = pd·(−0.2) + 0.8
out = pd·reed + breath;  clamp ±5;  write out·0.5
pole_state += lpf_c·(out − pole_state);  output += gain·env·pole_state
```

### 3.3 Elements Resonator = modal + BANDED WG bow
Same 64-mode math as Rings (`q = 500·10^(4·0.8·damping)`) + **first 8 modes doubled as banded waveguides**: delay(1/f_i) + SVF (q = 1+f_i·1500) BPN; loop `s=0.99·d.Read(); bow_signal+=s; s=f_bow(input+s); d.Write(s)`.
- **Elements BowTable:** `x = 0.13·velocity − x_string; b=6x; b=|b|+0.75; b=b²; b=b²; b=0.25/b; clamp[0.0025,0.245]; return x·b` ≈ `0.25/(|6x|+0.75)^4`.
- Modes >24 refreshed every other block (CPU trick).
- Position comb via cosine amplitudes; aux_amplitudes LFO 0.5Hz for stereo.

### 3.4 Voice topology (voice.cc)
```
env shape: <0.4 → AD percussive (gain 5−10x); 0.4−0.6 → ADSR; >0.6 → slow swell
bow  = FLOW (timbre scaled 0.4+0.6·brightness)   → bow_strength (banded WG)
blow = GRANULAR noise → Tube → diffuser (short granular smear)
strike = meta: ≤0.4 sample player, >0.4 particles, mallet middle
input = bow·env·bow_level·0.125·accent + blow·env·accent + strike·strike_level(≤1.5)
strike_level > 1.0 → strike_bleed = raw exciter to OUTPUT   // exciter bleed law
blow_level  > 1.0 → tube_level rises
damping -= strike.damping()·strike_level·0.125 + (1−bow_strength)·bow_level·0.0625
→ modal resonator (or 1 dispersion string, or 5-string chord bank w/ hysteresis ±0.1)
```
Chord table 5×11: `{0,−12,0,0.01,12}, {0,−12,3,7,10}, ... {0,−12,5,7,12}`.

---

## 4. PLAITS physical_modelling — the Rings→3-knob compression
- **Resonator**: 24 modes, batched 4 (SIMD-friendly); `q = 500·(2^(damping·79.7/12))²` (LUT-free!); `brightness ×= (1−structure·0.3)(1−damping·0.3)`; per-mode HF atten `1−2f`; fixed position=0.015 baked at Init; **NthHarmonicCompensation(3, stiffness): f0 /= stretch of 3rd partial → PITCH STAYS CORRECT as inharmonicity rises (PORT!).**
- **String**: Rings minus FIR damper/position comb (SVF LP only, cutoff = `12+d²·60+B·24` semitones), stretch_correction `clamp(160/SR·delay, 1, 2.1)`: `main_delay = delay − ap_delay·(0.408 − stretch_point·0.308)·stretch_correction`; input clamp ±20.
- **StringVoice**: excitation = one-period noise burst; cutoff `4f0·2^((B(2−B)−0.5)·72/12)`, q=0.5/1.0; sustain = **Dust** (density `5e−5+0.99995·B⁴`, amp `(8−6·dust_f)·accent`); accent bumps brightness & damping `0.25·accent·(1−x)`; aux = raw exciter.
- **ModalVoice**: strike `amp=(0.12+0.08·accent)·(1−damping·0.5)`, `temp[0]=amp·2^(fc²·24/12)/fc`, LP'd; cutoff range 60/36 semitones, q 1.5/0.7.
- **StringEngine**: 3 strings round-robin; on trigger, OLD f0 from 14-sample-delayed history → previous string keeps ringing at struck pitch — **free 3-voice per-osc poly trick.**

## 5. DaisySP — cleanest porting template (MIT)
Single-sample `float Process(in)`, zero stmlib deps. `damping_compensation = 1 − 2·atan(1/ratio)/2π` closed-form. **Two port bugs to avoid**: (1) `mode_amplitude_[i] = cos(position·2π)·0.25` — same ∀ modes (position comb lost; original = cosine SEQUENCE); (2) CalcStiff mid-segment LINEAR (original exponential). Use their structure, MI's math.

## 6. FAUST physmodels.lib (5115 lines)
- **modeFilter(freq,t60,gain)**: biquad `b={1,0,−1}`, `r = 0.001^(1/(t60·SR))`, `a1=−2r·cos(2πf/SR)`, `a2=r²` — **canonical T60→pole-radius mapping**.
- **bridgeFilter(brightness,absorption)**: 3-tap `h0=(1+B)/2, h1=(1−B)/4`, `rho = 0.001^(1/(320·t60))`, `t60=(1−absorption)·20 s`; guitar bridge = `−bridgeFilter(0.4,0.5)`, elec `−(0.8,0.6)`, violin nuts `−(0.6,0.1)`.
- nylonString stiffness=0.4, steelString=0.05 (si.smooth dispersion coeffs).
- violinBowTable = STK's exactly. fluteJetTable = `x(x²−1)` clamped. brassLips confirms STK constants. clarinetReed `(0.7, −0.44+0.26·n)`.
- **Exciters**: strike = noise → HP(40+pos·500) → LP(500+pos·15000) → AR(0.002·sharp); pluckString = noise LP at `5·f·cutoff`, att `0.002·sharp·(1−f/2000)²`; **blower = pressure + sin(vib)·vibGain + LP₂(noise, 2000Hz)·pressure·breathGain (0.005–0.05, vib 5Hz/0.03)**.
- **djembeModel**: 20 modes, `f_i = f0 + 200·i` Hz (constant spacing = membrane-ish), `t60_i = (20−i)·0.03 s`, `gain_i = 1/(i+1)²`.
- **marimbaBarModel**: 50 measured ratios `{1, 3.31356, 3.83469, 8.06313, 9.44778, 14.1169, 18.384, 21.0102, 26.1775, 28.9944, 37.0728, ... 195.505}`, 5 strike positions × 50 gains, **T60 law: `t60_i = t60·(1 − (ratio_i/ratio_max)·decayRatio)^decaySlope`** + resonator tube (waveguide f2l(f0), refl 0.99·smooth(0.95)) = the marimba "hollow bloom".
- **churchBellModel**: 50 measured Hz `{451.918, 455, 864.643, 871.402, 1072.47, 1073.98, 1292.23, ...4033.97}` (beating pairs!), 7 strike positions, T60 law (t60=30s, slope 2.5); english/french/german/russian variants.

## 7. CSOUND (LGPL: concepts/constants only)
- wgflute: `boreDelay = 1.5·SR/f − 2.0`; DC blocker pre-jet (older; do post-jet). kjet 0.08–0.56, pressure ~0.9–1.1.
- wgbow: bow slope = kpres RAW 1..5; refl pole `0.6 − 0.1·rate`, gain 0.95; single body biquad (500Hz, r 0.85, 0.2); "speaks" region = pressure 3±1.5, vel 0.03–0.23.
- wgbrass: confirms STK constants exactly.
- **marimba modal4.c: multi-strike randomization — 20% triple strike, 40% double (roll humanization), stick recoil.**

## 8. SURGE XT StringOscillator (GPL — CONCEPTS ONLY, re-derive)
Two KS delay lines (sinc 16384, SSE), Balance mix; per-line: read → add excitation → hard clip ±1 → tone filter (LP if stiffness<0 else HP!) → ×feedback → write. Out → soft clip `x·(1.5 − 0.5x²)`.
- **Exciter menu (16)**: burst {noise, pink/dust, sine, ramp, tri, square, sweep} = pre-filled (chirp KS), constant {same 7} = continuous, + audio-in (route ANY signal through the string!). Burst amp = `d0^0.25`; constant = `d0⁴`.
- **Decay→feedback knee law:** `fb = p<0.2 ? 0.85 + 0.5p : 0.9375 + 0.0625p` (knee at 0.95); extend mode bipolar (negative fb = octave-down square-ish).
- **Stiffness = ±tone filter IN LOOP with measured retune tables**: `{−0.0591,−0.1224,−0.2257,−0.4061,−0.7590}` st (LP side), `{0.0275,0.0903,0.31,0.615,0.87}` (HP side) — LESSON: in-loop filters detune; compensate via tables or phase-delay math.
- 2 strings: detune ±16 st or ABSOLUTE Hz mode (constant beat rate!); FM into delay-time.

---

# TOP 10 PORT LIST (ranked)
1. **Rings/Plaits modal resonator core** (MIT) — SVF mode bank + ComputeFilters (stiffness walk 0.93/0.98, q=500·10^(4d), q_loss brightness law, cosine-position amplitudes, NthHarmonicCompensation). THE mallets/bells/bars engine + "Structure" knob. Plaits 24-mode batched per voice; DaisySP as syntax template (fix 2 bugs).
2. **Rings KS string w/ dispersion & curved bridge** (MIT) — RT60-exact damping, FIR h0/h1 + SVF LP w/ phase compensation, bipolar nonlinearity (piano allpass / sitar rectifier), infinite-sustain ≥0.95. Guitar/lute/harp/piano/sitar in one class.
3. **The 4 macro mappings** (MIT) — Structure→inharmonicity (harmonic dead zone), Brightness→(q_loss + exciter cutoff), Damping→T60 exponential, Position→cosine comb / delay tap. Wire to uni-pill knobs verbatim.
4. **STK Bowed + BowTable + Maestre body SOS** (MIT) — friction `(|v·slope|+0.75)^−4`, slope=5−4·pressure, β=0.127236, maxVel=0.03+0.2amp, 6 body biquads. Complete violin/cello; body filter once per voice-sum.
5. **Elements exciter suite** (MIT) — Mallet (gain-compensated pulse), Plectrum (negative pre-pull), Particles, Flow (telegraph bow), granular breath, timbre LP 32Hz·10^(2.7t), accent 10^(1.5(x−0.5)), palm-mute feedback, bleed law. Our exciter panel.
6. **STK Clarinet + Brass loops** (MIT) — reed `clamp(0.7 − 0.3pd)`, slope −0.44+0.26n, breath 0.55–0.85; lip biquad r=0.997 at f·4^(2n−1), dp² scattering, slide 2·SR/f+3. ~25 lines each; Elements tube as ultra-cheap 3rd.
7. **STK Flute** (MIT) — jet `x³−x`, jetRatio 0.32 (0.08–0.56), refl/end 0.5/0.5, pole 0.7−0.1·ratio, breath 1.38–1.63, noise 0.15 + Faust blower (vib 5Hz·0.03, LP 2000Hz).
8. **STK BandedWG + Tibetan-bowl pairs** (MIT) + Elements 8-mode banded-bow. Bowed bars/glass/bowls; detuned mode-pair tables unavailable elsewhere.
9. **Modal preset tables** — STK ModalBar 9 presets + Faust marimba 50 ratios + churchBell 50 Hz + T60 law + djembe f0+200i. Seeds material/preset menu; strike-position sine gains + hardness law free.
10. **Excitation shaping trio** — Rings plucker.h (position comb burst) + Plaits Dust (density B⁴) + re-derived Surge decay-knee & burst-vs-constant exciter split with audio-in-as-exciter. Makes every model strummable, sustainable, feedable with our oscs/samples as exciters.

Cross-cutting: recompute mode coefficients per block (stagger modes >24 across blocks); allpass-interpolated main delay (alpha∈[0.5,1.5]) or Hermite; ALWAYS subtract filter phase delay from loop length; DC-block every wind/bow loop; clamp string state (±20 / ±5 / ±1) as self-heal guard; freq-dependent loop gain `g + f·5e-6` keeps decay constant across keyboard.
