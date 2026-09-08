# RESEARCH STREAM 3 — ACADEMIC EQUATIONS & MEASURED TABLES (agent complete, verbatim notes)

Conventions: ASCII math. f0 = fundamental, fs = sample rate, eta = loss factor = 1/Q. UNVERIFIED = not confirmed against primary source (fine for synthesis defaults).

## 1. PIANO

**1a. Inharmonicity.** Fletcher (JASA 36, 1964): `f_n = n*F*(1 + B*n^2)^(1/2)`. `B = pi^3*E*d^4/(64*T*L^2)`. Closed forms (SI):
- plain steel: `B = 3.95e10 * d^2 / (L^4 * f0^2)`
- copper-wound: `B = 4.6e10 * d^4 / (D^2 * L^4 * f0^2)`
Measured B, U-shaped across keyboard, min near C3: C1 ~4.0e-4 · C2 ~1.6e-4 · C3 ~1.4e-4 (min) · C4 ~3.5-4.0e-4 · C5 ~9.4e-4 · C7-C8 ~1e-2..2.5e-2 (Steinway D treble ~0.025; concert-grand bass ~2-3e-4).
Synthesis rule: log10(B) piecewise-linear vs key: ~−3.4 at A0 → −3.85 at C3 → ~−1.6 at C8. This B curve IS the Railsback stretch driver.

**1b. Unison detune + two-stage decay.** Kirk (JASA 31:1644): preferred 3-string unison mistuning = **1-2 cents max deviation**; zero-beat AND ≥3 cents both less preferred. Weinreich (JASA 62:1474): Eb3 — **prompt sound ~8 dB/s, aftersound < 2 dB/s**. Mechanism: symmetric pair pumps bridge (fast); antisymmetric sees rigid bridge (slow); vertical polarization decays faster than horizontal. 0.1-0.3 cent mistuning already sculpts aftersound. Cheap model: 2 strings/note detuned ±1 cent PLUS one "slow" mode per partial at −12..−20 dB with 4× the T60, or 2×2 bridge coupling matrix.

**1c. Hammer.** `F = K*x^p`. Measured p (Hall & Askenfelt/Russell): used hammers 2.2-3.5, dynamic bass p~2 → treble p~4, "preferred" 2<p<3. Simulation anchors (Chaigne & Askenfelt): **C2: K=4e8, p=2.3 · C4: K=4.5e9, p=2.5 · C7: K=1e12, p=3.0**. Stulov continuous fit (key n=1..88): `Q0 = 183*exp(0.045n)` N/mm^p, `p = 3.7+0.015n`, hysteresis alpha = 248+1.83n−0.055n² µs (different felt model family — pick ONE; Chaigne triplet safest).
- Contact time: **~4 ms bass, ~3 ms C4, <1 ms top treble; ±20% pp→ff (shorter=louder=brighter)**. Bass contact ~10% of period.
- Hammer velocity: **0.11 (pp) → 6.83 m/s (ff)**; pp~1, mf~2-3, ff~5. Key press-to-bottom 160 ms (p) → 25 ms (f).
- Hammer mass ~11 g bass → 4-5 g treble.

**1d. Strike point.** Books: d/L = 1/7..1/9; real grands: bass slightly <1/8 decreasing to ~A4, top treble **1/12..1/17** (per-note for loudness). Mode weight `g_n ∝ sin(n*pi*d/L)`.

**1e. Longitudinal modes + phantom partials** (Bank & Sujbert JASA 117:2268):
- First longitudinal mode ~**15.8× f0** in bass (F1: ~690 Hz), second ~2×.
- Phantom partials at `f_m ± f_n`; odd phantoms from adjacent parents (f_n + f_(n+1)); phantom series inharmonicity = **B/4**: `f_p ≈ f0*p*sqrt(1+(B/4)p²)`. Dominant parents = transverse modes 10-20. Longitudinal free response decays fast (~0.15 s); phantoms persist.
- Excitation of longitudinal bank is QUADRATIC in transverse amplitude. Bridge impedance ~1000× string.
- Real-time recipe: transverse string → form force from parent-mode products → K 2nd-order resonators at longitudinal freqs → soundboard through HIGH-PASS path. ~10-15% cost of full FD.

**1f. Strings.** Plain-steel tension ~650 N uniform; string Q 1000-2000 in sims (flat ok). 1 wound ×~8 bottom, 2 wound ×~10, 3 plain elsewhere.

**1g. Soundboard** (Ege & Boutillon): modal density plateau ~**0.05 modes/Hz** (spacing ~20 Hz) up to **1.1 kHz**; above, ribs confine (inter-rib waveguides), density falls; **mean loss factor eta ~2% (Q~50)**. Cheap board: 10-20 resonators 20 Hz apart from ~50 Hz + statistical tail, or 2048-tap multirate IR; longitudinal input high-passed.

## 2. DAMPING / MATERIAL LAWS

**Core:** `tau = 1/(pi*f*eta)`; `T60 = 2.2/(f*eta)`; `Q = 1/eta`. Constant-eta ⇒ T60 ∝ 1/f (alpha=1).

**Aramaki/Kronland-Martinet law** (IEEE TASLP 19:301, 2011): per-mode decay rate `alpha(w) = exp(aG + rG*w)`. **Metal: aG = 0.332, rG = 4e-5** (verified). rG (HF-vs-LF decay slope) = THE dominant material cue: metal small rG; glass low aG, faster HF falloff; wood large rG (~1e-3-ish UNVERIFIED — calibrate by ear). Morph materials by interpolating (aG, rG).

**Measured eta (=1/Q):** steel cello string transverse Q≈500, torsional Q≈45 (Woodhouse); piano string Q 1000-2000; spruce soundboard eta≈0.02; xylophone wood bars: `1/t_d = a0 + a1*f²` (needs BOTH constant + f² loss); bar wood density 0.80-0.95 g/cm³, E 15-20 GPa. Handbook eta ranges (UNVERIFIED): aluminum 1e-5..1e-4; steel/bronze/brass 1e-4..1e-3; glass 1e-3..2e-3; hardwood 5e-3..2e-2; softwood 2e-2..5e-2; nylon/gut 1e-2..5e-2; mylar head ~1e-2..5e-2.

**Perception** (Klatzky/Pai): decay parameter tau_d = most powerful determinant of perceived material; register secondary. To make one modal skeleton read as different materials: keep ratios, swap (aG, rG) + spectral roughness (metal = near-degenerate pairs 1-5 Hz apart for shimmer).

**Material-izer recipe:** (1) mode set; (2) per mode `r1 = exp(-alpha(w)/fs)`, alpha(w)=exp(aG+rG*w); (3) metal aG~0.33 rG~4e-5; glass rG 2-4× metal, aG low; wood rG 20-50× metal, aG higher; (4) f² term for wood bars; (5) morph = lerp (aG,rG) + crossfade spectra.

## 3. PERCUSSION MODE TABLES

**Free-free bar:** 1 : 2.756 : 5.404 : 8.933 : 13.34 : 18.64. Clamped-free: 1 : 6.267 : 17.55 : 34.39 (UNVERIFIED).
**Xylophone:** undercut → **f2 = 3·f1**; closed-pipe resonator reinforces f1. Contours exist for 1:3:6, 1:4:8, 1:4:9.
**Marimba:** **f2 = 4·f1** exactly (first 3-3.5 octaves), **f3 = 10·f1** (first 2 octaves), f4 ~20×(low)→6×(high). First TORSIONAL: 1.9×(large)→1.2×(small) — silent center-strike, excited off-center. Bork & Meyer preference: **f3 = 9.88·f1** sounds best. Vibraphone ~1:4:10 + tremolo.
**Ideal circular membrane** (ratios to (0,1)): 1.000, 1.594, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501, 3.600, 3.652, 4.060, 4.154.
**Timpani:** air-loading + kettle pull (m,1) modes near-harmonic; **(0,n) modes = monopoles, die instantly, musically absent** (strike ~1/4 radius). Measured: (1,1)=principal, (2,1)=1.504, (3,1)=1.742, (4,1)=2.00, (5,1)=2.245, (6,1)=2.494 → tune (m,1) series **1 : 1.5 : 2 : 2.5 : 3**; keep 2-4 off-series modes; (0,1)/(0,2) T60 < 100 ms.
**Snare drum** (35 cm): coupled head PAIRS: (0,1) 182/330 Hz, (1,1) 278/341, (2,1) 403, (0,2) 445; snares = noise band above ~1 kHz.
**Djembe:** Helmholtz 70-80 Hz, membrane cluster 400-800 Hz, partials to 3 kHz.
**Church bell:** hum:prime:tierce:quint:nominal:upper-third = **0.5 : 1.0 : 1.2 : 1.5 : 2.0 : 2.5** (→ ~3.0, 4.0). Strike pitch = octave below nominal (virtual). Major-third bells: tierce 1.25. Each mode a DOUBLET: Western bells split few cents (slow warble); Chinese two-tone 200-500 cents. Handbell: (3,0) = 3× exactly. Clapper = Hertzian: contact time shrinks with velocity (loud=brighter), width ∝ v^(-1/5) (UNVERIFIED exponent).
**Tubular bells:** free-free bar series; modes 4:5:6 ≈ 2:3:4 ⇒ virtual strike pitch at half of mode 4 (missing fundamental).
**Gong/tam-tam:** Chinese opera large gong glides DOWN up to 3 st; small gong UP ~2 st (measured). Tam-tam cascade: strike → 700-1000 Hz buildup 10-20 ms → 3-5 kHz shimmer dominating ~1 s. CHEAP FAKES (physics-endorsed): (1) pitch glide = amplitude-envelope-driven detune, tau 0.3-1 s, ±2-3 st by type; (2) brightness bloom = envelope follower crossfading energy into pre-built 3-5 kHz mode cloud, 100 ms-1 s lag, gated on strike level; (3) shimmer = dense detuned high-mode pairs.

## 4. BOWED STRING

**Friction curve** (Woodhouse 2003, fit to rosin measurements):
`mu(v) = 0.4*exp(-v/0.01) + 0.45*exp(-v/0.1) + 0.35` ⇒ **mu_static = 1.2, dynamic tail → 0.35**. Effective mu during Helmholtz ≤ ~0.77.
**Junction solve** (Friedlander): `v = v_h + (f/2)(1/Z_T + 1/Z_R)`; `f = N*mu(...)*sgn(v_b − v)`; intersect load line with friction curve (table lookup).
**Schelleng playability window (exact):**
- `f_max = 2*Z0*v_b / (beta*(mu_s − mu_d))` (∝ 1/beta)
- `f_min ≈ Z0²*v_b / (2*R*beta²*(mu_s − mu_d))` (∝ 1/beta²)
- typical: N = 0.01-10 N, beta = 0.02-0.4, v_b ~0.05-0.5 m/s. Too little force → double-slip "surface sound"; too much → raucous. **A PLUGIN MUST auto-clamp (beta, force) inside the wedge, margin ~1.5×.**
**Calibrated cello D string:** f0 147 Hz, transverse Q≈500, B = 3e-4 N·m², Z = 0.55 N·s/m, torsional Z 1.8, torsional Q≈45.
**Violin body (measured):** **A0 = 272-275 Hz** (Helmholtz, always radiates), CBR ~400-407 (weak), **B1− = 462 (450-480)**, **B1+ = 551 (530-570)**, **bridge hill ~2.3-3 kHz** (broad), 2nd hill ~6 kHz. Cello A0 ~100-110 Hz (UNVERIFIED), hill ~1 kHz UNVERIFIED. Starting Qs: A0~12, corpus ~25-50, hill ~8. Fake body = 4-8 peaking biquads: A0, B1−, B1+ strong (+10-15 dB), CBR small, dip 1.2-1.5k, hill wide +6-10 dB.
**Recipe:** 2 delay pairs around bow point (beta); mu(v) table + Friedlander; Schelleng clamp; torsional loss shunt (Q45); body biquads; vibrato = delay-length mod ~6.5 Hz.

## 5. WINDS / BRASS OPERATING POINTS

**Clarinet** (Almeida et al. JASA 134:2247, measured): blowing ~3 kPa typical; threshold 3-4 kPa; extinction 6.5-7.5 kPa ⇒ playable **3-7 kPa**. Reed closing pressure p_M ≈ 4 kPa; ideal threshold = p_M/3. Lip force 0.3-2.8 N. Squeaks at 1.0-1.2 kHz (low lip force / low damping corner). **Spectral centroid 500-2500 Hz tracks level** (crescendo = brighten!). Cylindrical closed bore → odd harmonics, register = 12th; even harmonics rise toward **~1.5 kHz lattice/bell crossover**. CONICAL (sax/oboe) = ALL harmonics, overblows octave — this IS the clarinet-vs-sax switch.
**Flute:** loop = bore delay ONE period; **jet delay = HALF bore period** (the phase condition); cubic x−x³; breath noise few % essential. Bernoulli u_j = sqrt(2P/rho): 0.2-2.5 kPa → 18-64 m/s. Overblow: jet delay halves (pressure ~4× = octave).
**Brass:** mouth pressure <1 kPa (soft) → ~10 kPa (loud). Lips = pressure-controlled valve tracking played note (2nd-order resonator retuned per harmonic). Bell: reflects LF/transmits HF — **trumpet: no returned energy above ~1500 Hz** (Benade); impedance envelope peaks ~**850 Hz** (mouthpiece Helmholtz boost; model as +4-8 dB peaking biquad in loop); trombone megaphone above ~700-800 Hz. Reflectance = gentle LP cutoff ~1.3-1.5 kHz (trumpet) / 0.7-0.8 kHz (trombone); radiated = 1 − reflection (HP). Pedal notes = mode-locking. **Brassiness at ff = nonlinear steepening → level-dependent tanh INSIDE/after bore, drive tracks RMS** — must be signal-dependent, not EQ.

## 6. EXPRESSION / HUMANIZATION (measured)

- **Violin vibrato: 5.1-8.2 Hz (mean ~6.5); extent mean ~14 cents, range 4-32** (Mellody & Wakefield). Viola ~38 cents; cello 5.24 Hz, 30-50 cents.
- Singers: 4.6-8.7 Hz (avg 6.0), extent ±71 cents (Prame).
- Winds/flute: ~5 Hz, mostly amplitude+brightness mod, ≤10 cents pitch (UNVERIFIED).
- Vibrato onset delay ~0.3-0.7 s, ramps in (ship 0.4 s default, 0.3 s ramp).
- Legato: violin ~30 ms measured; strings 30-80 ms finger change, portamento 100-300 ms; winds 10-30 ms.
- Onset pitch settle: 30-60 ms exponential from −20..−60 cents at hard attacks, 0 at soft (brass/reed regime-lock 20-80 ms).
- Per-note randomization that reads human: pitch ±1-2 cents; contact-time jitter within ±20% law; strike/pluck point ±2-5% of L; bow force/speed drift = pink noise 0.5-2 Hz few %.
- Piano action timing: soft notes land LATE (160 ms) vs loud (25 ms) — delay soft notes.

## 7. MODAL IMPLEMENTATION

**Resonator forms** (Mathews & Smith SMAC 2003, verified):
- **2D-rotation (complex multiply) — USE for time-varying f/damping:**
```
x[n+1] = x1*x[n] − y1*y[n] + u[n]
y[n+1] = y1*x[n] + x1*y[n]            (output y)
x1 = r1*cos(th1), y1 = r1*sin(th1), r1 = exp(−1/(tau*fs)), th1 = 2*pi*f/fs
```
Changing f or tau per-sample = NO amplitude discontinuity (rotation preserves state energy) — direct-form biquads click. 4 mul/sample. Thousands of modes trivial on modern CPU.
- **Magic circle** (fixed-point robust, best LOW freqs): `x[n+1] = r1*(x[n] − eps*y[n]) + u; y[n+1] = r1*(eps*x[n+1] + y[n])`, eps = 2*sin(th1/2).
- **DWR** (2 mul/sample, tuning via ONE coefficient).
- **Phase-preserving restrike:** scale state magnitude or inject at output zero-crossings — re-excite a ringing mode without clicks (mallet rolls/tremolo).

**Excitation pulse:** raised cosine width = contact time; spectrum rolls ~12 dB/oct above f ~ 1/t_contact. Piano 1-4 ms; mallets hard 0.5-2 ms, soft 5-20 ms; shrink width 20-30% pp→ff.
**Strike-position weighting:** `g_n = sin(n*pi*p)` (1-D); hammer width w multiplies sinc-ish LP. Membranes: J_m(k_mn*r). Pluck (displacement): ∝ sin(n·pi·p)/n²; strike (velocity): ∝ sin(n·pi·p)/n.
**Mode budgets:** piano 50-100 transverse modes/note (Bank & Sujbert explicitly) + 2-6 longitudinal; bars 4-6 + 2-3 torsional; bells 7-12 incl. doublets; timpani/drums 8-15; body/soundboard 10-30 + tail. Prune: keep within ~40-60 dB of strongest.

## MINIMAL VIABLE REALISM (topology + 5 numbers per family)

- **PIANO**: 2-3 detuned strings/note → bridge → soundboard IR. (1) B: 1.5e-4 C3 → 1e-2..2.5e-2 C8, 3e-4 bass; (2) hammer p=2.3/2.5/3.0, K=4e8/4.5e9/1e12 at C2/C4/C7, contact 4→1 ms; (3) unison ±1-2 cents, prompt 8 dB/s vs aftersound 2 dB/s; (4) strike point 1/8; (5) longitudinal formant ~16×f0 + phantoms (B/4 series), board eta 2%.
- **GUITAR/LUTE/HARP**: plucked string, comb sin(n·pi·p)/n², 2 polarizations detuned <1 Hz. (1) Q~500 (nylon lower); (2) body A0 ~100 Hz + top ~200 Hz, Q 15-40; (3) pluck point 1/5..1/12; (4) low-level phantoms; (5) jitter: point 2-5%, pitch 1-2 cents.
- **VIOLIN/CELLO**: waveguide + friction table + body biquads. (1) mu(v) above; (2) Schelleng clamp, beta 0.02-0.4, v_b 0.05-0.5, N 0.3-3; (3) Z0 0.55, Q500/torsion45; (4) body 275/460/550 + hill 2.5k (cello ~100 + ~1k); (5) vibrato 6.5 Hz 15-30 cents onset 0.4 s.
- **FLUTE**: bore delay + jet delay T/2 + x−x³ + noise. (1) jet = half period — THE number; (2) 0.2-2.5 kPa; (3) noise few %, breathy onset; (4) overblow = jet/2; (5) vibrato 5 Hz mostly amplitude.
- **TRUMPET/HORN**: lip valve (tracks note) + bore + bell split. (1) bell cutoff ~1.4k trumpet / 750 trombone; (2) mouthpiece peak ~850 Hz biquad; (3) pressure 1→10 kPa = level; (4) brassiness = RMS-driven tanh; (5) attack settle 30-60 ms.
- **CLARINET/SAX**: reed table + bore. (1) p_M ~4 kPa, threshold p_M/3, extinction ~7 kPa; (2) lip force dims/raises pitch; (3) cylinder=odd+12th, cone=all+octave (THE sax switch); (4) lattice cutoff ~1.5 kHz; (5) squeak guard ~1-2 kHz corner.
- **MARIMBA/VIBES**: 4-6 modes + pipe on f1. (1) 1:4:10 (xylo 1:3:6, best f3=9.88); (2) torsional 1.2-1.9× off-center only; (3) wood loss a0 + a1·f²; (4) contact hard 1-2 ms / soft 5-20 ms; (5) pipe = BP Q~30 on f1 (vibes +5 Hz trem).
- **BELLS**: 7-12 partials 0.5:1:1.2:1.5:2:2.5(:3:4); doublets 0.1-1% (warble); T60 tens of seconds (eta ~1e-4); Hertzian strike (loud=bright); strike pitch = octave below nominal (free).
- **TIMPANI/DRUMS**: (1) (m,1) tuned 1:1.5:2:2.5:3, (0,n) T60 <0.1 s; (2) toms ideal Bessel; (3) strike 1/4 radius; (4) T60 0.5-4 s falling with f; (5) snare = coupled pairs + gated noise; gong = amplitude pitch glide ±2-3 st + bloom into 3-5 kHz cloud.

## KEY SOURCES
JOS PASP chapters · Woodhouse Acta Acustica 89:355 (2003) · euphonics.org 9-3-1/5-3/7-3/11-2/12-2-1 · Bank & Sujbert JASA 117:2268 · Rossing AST 22:177 + 25:406 · Almeida et al. JASA 134:2247 + UNSW pages · Benade Trumpet 1973 · Mathews & Smith SMAC03 · Kirk JASA 31:1644 · Weinreich JASA 62:1474 + KTH 5-lectures · Ege/Boutillon arxiv · Aramaki IEEE TASLP 19:301 · Klatzky/Pai Presence 9:399 · van den Doel & Pai Presence 7:382 · Russell PSU · hyperphysics timpani.
