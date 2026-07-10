# MODAL — the physical modeling engine (definitive research synthesis)
2026-07-10 · engine slot 7/8 (spectral was already GEODE — modal is the last engine)
Raw research (implementation-grade formulas, tables, teardowns): `.ideas/modal-research/research-{1..4}-*.md`
Research provenance: 4 parallel streams — (1) source-code mining of STK/Rings/Elements/Plaits/DaisySP/Faust/Csound/Surge (files read directly, licenses cleared), (2) commercial teardowns from primary manuals (Pianoteq, Chromaphone 3, Sculpture, SWAM, String Studio VS-3, Collision, VL1, V-Piano, GeoShred), (3) academic extraction (JASA/CMJ/DAFx/PASP measured numbers), (4) 105-agent verified deep-research sweep (14 findings, all 3-0 adversarial votes).

---

## 0. VERDICT — the architecture in one paragraph

One header-only `ModalEngine.h` (HarmonicEngine.h pattern: no-JUCE, per-voice, live-retune, declick render) built as **EXCITER → RESONATOR → BODY**, where the resonator is one of **three verified cores behind a single voice API**, selected per FAMILY:

- **(a) STRING** — dual-polarization single-delay-loop pair (Rings/CMJ98 KS string: RT60-exact damping + phase-compensated loop filters + Hermite/allpass fractional delay), optional closed-form Thiran **dispersion cascade** (Rauhala-Välimäki: live inharmonicity-B knob, 8add/6mul/1div/1log/3exp per update) → piano, guitar, lute, harp, sitar, clav.
- **(b) MSW LOOP** — the same delay loop with a **nonlinear table in-loop** (McIntyre-Schumacher-Woodhouse, JASA 1983 — verified: ONE architecture covers all sustained instruments by swapping only the nonlinearity F, the reflection filter, and constants): bow-friction `(|v·slope|+0.75)^-4`, reed `clamp(0.7−0.3·pd)`, delayed air-jet `x−x³`, lip biquad `r=0.997 @ f·4^(2n−1)` + dp² scattering → violin, cello, flute, clarinet/sax, trumpet/horn.
- **(c) MODAL BANK** — importance-sorted 2-pole resonator bank (Mathews-Smith **2D-rotation form**: retune per-sample with zero clicks, ~3-4 mul/mode/sample; verified budget: 800-1000 modes ran on a 450 MHz CPU in 2003) with measured ratio tables → bells, bars, membranes, plates, bowls; doubles as the sympathetic/soundboard bloom bank behind (a)/(b).

**BODY** = commuted synthesis for plucked (body IR pre-convolved into the excitation = near-zero runtime cost, verified PASP+CMJ98) + biquad body sets for bowed/brass (Maestre 6-biquad violin body shipped in STK, MIT) + soundboard resonator tail for piano. **Sample-as-exciter is a first-class source** (the "noise into guitars" moat — Rings external-audio exciter path + Surge audio-in exciter concept + Pigments 6 collision/friction validation). All numbers below have citations in the research files.

FDTD verdict (verified, Bilbao): most general, numerically fragile, **not** the per-voice real-time path; waveguide efficiency provably vanishes in 2D/3D → drums/bells/plates are MODAL BANKS, not meshes. FDTD is offline/mode-data territory only.

Licensing: STK, Mutable (Rings/Elements/Plaits), DaisySP, Faust models = MIT/permissive, **port-safe**. Csound (LGPL) + Surge (GPL) = concepts/constants only. Old waveguide patents expired ~2006-2014.

---

## 1. THE 10 CONTROLS — 5 ESSENTIALS + 5 SCULPT

Layout = the harm-knob-wrap pattern verbatim: two pages of 5 knobs. Every knob is one physics law, family-aware, LFO/env-draggable (KNOBDEST), and safe at every position (auto-clamped operating windows — see §6).

### Page 1 — ESSENTIALS (the physics every instrument shares; validated: these appear in EVERY serious PM product)

| # | Name | Physics | DSP mapping (from research) |
|---|------|---------|------------------------------|
| 1 | **HARD** | Exciter hardness / contact time — the universal brightness-of-attack law | Excitation pulse width: soft 5-20 ms → hard 0.3-2 ms (raised cosine; spectrum rolls 12 dB/oct above 1/t_contact). Exciter LP cutoff = `32Hz·10^(2.7·hard)` (Elements law) with gain-compensated pulse `(0.42/f)·4^(f²)` so loudness stays constant. Velocity couples in: contact −20-30% pp→ff (measured piano law), plus vel→cutoff (verified PASP dynamics law). Piano felt: `F=K·x^p`, p 2.3→3.0, K 4e8→1e12 across the keyboard, baked per-family, HARD scales it. |
| 2 | **POS** | Excitation position — the node/comb law, cheapest huge timbre lever | Strings: pluck comb `sin(n·π·p)` (Rings frequency-domain cosine amplitudes for modal = flange-free when modulated; delay-tap comb for SDL). Modal: pure gain-reweight (verified: position NEVER recomputes modes). Bow: β (bridge 0.02 ↔ 0.4 tasto). Winds: jet ratio 0.08-0.56 / blow position. Skins: Bessel weights J_m(k·r), center kills ring modes. Defaults per family (piano 1/8, guitar 1/5-1/12, bow β=0.127). |
| 3 | **DECAY** | Global damping / T60 (the frequency-INDEPENDENT loss — "media loss") | RT60-exact: `rt60 = 0.07·2^(8·d)` s, per-trip gain `2^(−10·delay/rt60)` hitting −60 dB exactly (Rings law); ≥0.95 crossfades to infinite sustain. Note-off = DAMPER, not VCA: release decay = fraction of held decay (String Studio law), damper efficiency ties to MATERIAL; ring-tail declick law enforced (existing house rule). |
| 4 | **MATERIAL** | Frequency-DEPENDENT damping tilt — the wood↔glass↔metal axis (perceptually validated as THE material cue) | Bipolar. Per-mode decay `alpha(w) = exp(aG + rG·w)`: metal aG≈0.33, rG≈4e-5; glass rG 2-4× metal; wood/felt rG 20-50× metal (+ f² loss term for wood bars). Morph = lerp (aG,rG). For loop cores = loop-filter cutoff tilt (`24+d²·48` semitone law + brightness q_loss `b(2−b)·0.85+0.15`). Also adds metal shimmer at the bright end: near-degenerate mode pairs split 0.1-1% (Tibetan-bowl beating-pairs table). Two damping laws (DECAY ⊥ MATERIAL) = the #1 anti-fake doctrine from the teardowns. |
| 5 | **BREATH** | The stochastic + continuous excitation lane — noise is half the instrument | 0 → pure one-shot strike/pluck (plus its natural chiff scaled by HARD). Up → the family's continuous drive fades in: bow = Flow telegraph noise into the friction junction, winds/brass = blowing pressure + turbulence (breath windows baked: clarinet 0.55-0.85, flute 1.38-1.63, mapped inside the speaking range), strings/bars = Dust sustained excitation (density `B⁴` law) / bowed-bar banded-WG drive. Struck families morph pluck→bowed/blown hybrids — every model becomes sustainable. Faust blower recipe: pressure + 5 Hz vib·0.03 + LP₂(noise,2 kHz)·0.005-0.05. |

### Page 2 — SCULPT (each one radically re-voices the instrument)

| # | Name | What it does | DSP mapping |
|---|------|--------------|-------------|
| 6 | **STRETCH** | Inharmonicity / geometry morph: membrane ← HARMONIC → piano-steel → bar → bell/gong | The Rings structure law verbatim (with harmonic DEAD ZONE 0.24-0.26): negative squeezes partials (drum/plate), 0 harmonic, up = exponential stretch `0.01·10^(2.005u)−0.01` → bell regime `1.5−cos(u·π)/2`. String cores: live-B Thiran dispersion cascade (4×2nd-order allpass keys 1-44, 1× keys 45-88; closed-form coefficients; ~20 biquads tune a low piano note to 6 kHz — verified). Pitch stays anchored via NthHarmonicCompensation (Plaits — tune the 3rd partial to the note). Real piano B curve baked per-key (1.4e-4 @C3 → 2.5e-2 @C8); STRETCH scales around it. |
| 7 | **BLOOM** | Post-attack life — energy migrates upward AFTER the strike (Pianoteq blooming + gong/tam-tam cascade + piano phantom partials; reads as "alive", no competitor has it as a knob) | Envelope-follower-driven: (1) crossfades strike energy into a pre-built 3-5 kHz mode cloud with 0.1-1 s lag, gated on strike level (tam-tam law); (2) amplitude-driven pitch glide ±2-3 st for gong regimes; (3) piano: quadratic phantom-partial bank at `f_n+f_(n+1)` with B/4 inharmonicity + longitudinal formant ~16×f0 through a high-pass path (Bank & Sujbert recipe, ~10-15% cost). |
| 8 | **HALO** | Sympathetic & coupled resonance — the energy-comes-back law (one-way chains sound dead) | Low: dual-polarization pair mistune 0.1-2 cents + asymmetric loop filters = beating + two-stage decay (prompt 8 dB/s → aftersound 2 dB/s, Weinreich; verified CMJ98 technique). Mid: duplex/aftersound shimmer. High: full sympathetic string set (Rings interval walk: [−12, −7.02, 0, +7.02, +12, +19.02, +24] with Squash snapping, per-string detunes {1.3, 1.1, 0.7, 1.7} cents, sluggish retune `2^((b−1)·36/12)`) fed by `0.2/n·(out−aux)` — pedal-down piano halo, open-string guitar ring, bell warble doublets. |
| 9 | **AGE** | Condition/imperfection macro with per-note reseed — the round-robin knob | Scales ALL bounded humanization: strike-point jitter ±2-5% L, hardness jitter ±10%, per-note pitch ±1-2 cents, contact-time ±20%, unison scatter growth, damper inefficiency, Csound marimba roll law (20% triple / 40% double strike at high AGE), curved-bridge rectifier crossfade at extremes (sitar/biwa buzz — Rings bipolar-structure negative side), key/damper noise level. 0 = mint instrument. Per-note re-rolls of PHYSICAL quantities read human; free-running mod reads chorus (teardown doctrine). Reseeded per note-on; 4 oscs carry 4 seeds → osc round-robin rotation = true instrument round robins. |
| 10 | **BODY** | The resonator body / capture morph — same string, different instrument | 0 = direct/raw (electric). Up = family body grows + morphs SIZE: plucked = commuted body IR pre-convolved into excitation (near-zero runtime, verified; ship synthetic IR sets: parlor→dreadnought→jumbo, lute bowl, harp board), bowed = Maestre 6-biquad violin body (MIT, coefficients in research-1) with formant-shift resize law (Sculpture ×10 log shift; cello = shift −1 octave + A0 100 Hz, hill ~1 kHz), piano = soundboard modal tail (10-20 resonators 20 Hz apart, eta 2%) + impedance cutoff/Q, brass = bell reflectance crossover morph (trumpet ~1.4 kHz ↔ trombone ~750 Hz + mouthpiece peak ~850 Hz), winds = tonehole-lattice cutoff. Top of range also widens stereo listening positions (Collision L/R law: odd modes L, even R). |

Header (hm3 selector-twins pattern, no new furniture): **FAMILY · FORM** — FAMILY regime-morphs the core (like HARM's HUE), FORM picks the curated variant recipe:

| FAMILY | core | FORMS (variant recipes — "the collection") |
|---|---|---|
| GRAND | (a)+dispersion | Concert · Upright · Tack · Felt · Silver |
| PLUCK | (a) | Steel · Nylon · Lute · Harp · Sitar · Mute |
| BOW | (b) friction | Violin · Viola · Cello · Contra · Gamba |
| FLUTE | (b) jet | Concert · Alto · Pan · Shaku · Bottle |
| REED | (b) reed | Clarinet · Sax · Oboe · Duduk (cyl↔cone switch = the sax law: cone → all harmonics + octave register) |
| BRASS | (b) lip | Trumpet · Cornet · Horn · Trombone · Tuba |
| BARS | (c) | Marimba · Vibes · Xylo · Glass · Bowl · Kalimba |
| BELLS | (c) | Church · Tubular · Hand · Gong · Tibetan |
| SKIN | (c) | Timpani · Tom · Snare · Djembe · Tabla |

Exciter source pill (uni-pill clone, small): **SRC = Auto / Noise / Click / Sample** — Sample = the osc's dropped sample buffer becomes the excitation (LP'd by HARD, enveloped by BREATH). Drop a snap → it rings a guitar body. This is the moat; nobody with a real multisampler feeds YOUR audio through per-voice physical models. (Future: Osc-as-exciter joins the cross-osc warp routing vision.)

---

## 2. FAMILY RECIPE TABLE — the 5 numbers that matter (full derivations in research-3)

| Family | Topology | The numbers |
|---|---|---|
| GRAND | dual-pol SDL ×2-3 strings + Thiran dispersion + soundboard tail + phantoms | B: 1.4e-4 (C3) → 2.5e-2 (C8), ~3e-4 bass; hammer p=2.3/2.5/3.0, K=4e8/4.5e9/1e12 @C2/C4/C7; contact 4→1 ms ±20%; unison ±1-2¢, prompt 8 dB/s vs after 2 dB/s; strike 1/8; longitudinal ~16×f0, phantoms B/4; board eta 2%, modes every ~20 Hz to 1.1 kHz; soft notes land up to +135 ms late |
| PLUCK | SDL + pluck comb + commuted body | loop gain g=0.995+f·5e-6 (STK freq-compensation law); Q~500 steel, lower nylon; pluck 1/5-1/12; plectrum = negative pre-pull then release (Elements); 2 polarizations <1 Hz apart; body A0 ~100 Hz + top ~200 Hz |
| BOW | MSW: 2 delays around β + friction table + body biquads | μ(v)=0.4e^(−v/0.01)+0.45e^(−v/0.1)+0.35 (static 1.2→0.35); slope=5−4·pressure; **Schelleng clamp**: F_max=2Z₀v_b/(β·Δμ), F_min=Z₀²v_b/(2Rβ²Δμ), margin 1.5×; β=0.127 default; Z₀=0.55 N·s/m, Q500 (+torsion Q45 loss); body 275/462/551 Hz + hill 2.5 kHz (violin), 100 Hz + 1 kHz (cello); vibrato 6.5 Hz, 14-30¢, onset 0.4 s |
| FLUTE | jet loop: bore delay + jet delay + x−x³ + noise | jet delay = bore·0.32 (≈ half period = THE phase condition; 0.08-0.56 usable, ~0.5 octave-jumps); f_loop=0.6667·f (overblow tuning); refl/end 0.5/0.5; pole 0.7−0.1·(22050/SR); breath 1.38-1.63 + 15% noise + 5 Hz vib; DC-block AFTER jet |
| REED | half-period delay + reed table | reed = clamp(0.7−0.3·pd), stiffness slope −0.44+0.26n; breath speaks 0.55-0.85; p_M≈4 kPa, threshold p_M/3; cylinder = odd harmonics + 12th, cone = all + octave; lattice cutoff ~1.5 kHz; squeak guard: avoid low-damping corner @1-1.2 kHz |
| BRASS | delay 2·SR/f+3 + lip biquad + bell split | lip r=0.997 tuned f·4^(2n−1) (lip tension SELECTS the harmonic — the trumpet magic); dp²-area scattering, mouth 0.3·breath, bore 0.85; bell reflect LP ~1.4 kHz (trumpet)/750 Hz (trombone), radiate = 1−refl; mouthpiece +4-8 dB @850 Hz; brassiness = RMS-tracked tanh drive (signal-dependent, never EQ); attack settle 30-60 ms from −20..−60¢ |
| BARS | modal 4-6 modes + pipe resonator on f1 (+ banded-WG bow via BREATH) | marimba 1:4:10 (best f3=9.88), xylo 1:3:6 (+ torsional 1.2-1.9× off-center only); STK 9-preset table (ratios/radii/gains) shipped; wood loss 1/t_d = a0+a1·f²; pipe = BP Q~30 on f1; vibes +5 Hz tremolo, glass {1,2.32,4.25,6.63,9.38}; bowl = beating pairs table |
| BELLS | modal 7-12 partials, doublet pairs | 0.5:1:1.2:1.5:2:2.5(:3:4); doublets split 0.1-1% = warble; hum/prime T60 tens of seconds (eta ~1e-4); Hertzian clapper: contact shrinks with velocity (loud=bright); strike pitch = octave below nominal (free psychoacoustics); tubular = free-free bar 2:3:4 virtual pitch |
| SKIN | modal membrane, kettle-tuned | timpani (m,1) → 1:1.5:2:2.5:3, (0,n) T60<0.1 s, strike ¼ radius; toms ideal Bessel 1:1.59:2.14:2.30:2.65:2.92; snare = coupled head pairs 182/330 + gated noise >1 kHz; djembe Helmholtz 70-80 Hz + 400-800 cluster; gong = BLOOM territory |

---

## 3. REALISM PLAYBOOK (the 12 laws from the teardowns — apply to every family)

1. Noise is half the instrument (hammer thump, chiff, rosin, breath, damper whoosh, key release — BREATH/AGE own these).
2. Two damping laws, never one (DECAY ⊥ MATERIAL).
3. Nonlinearity budget in the first 50 ms (felt law, tension-flare on plucks: attack pitch settle baked per family + scales with HARD/velocity; Sculpture Tension-Mod law) + slow BLOOM after.
4. Energy must come back (HALO; persistent string state on retrigger — voice reuse keeps resonator state like Sculpture).
5. Randomness bounded, physical, reseeded per note (AGE; never free-running).
6. Sustained instruments live on the continuous control stream (BREATH is env/LFO/MPE-draggable like every Terrain knob; velocity demoted to accent for BOW).
7. Release is a mechanical damper event (DECAY release law + damper noise via AGE).
8. Detuned pairs make sustain 3D (HALO low range — cheapest big win, dual polarization).
9. Instrument ≠ capture (BODY top range = listening positions/stereo).
10. Keyscale everything (all laws key-indexed: B curve, contact times, hammer K/p, body fixed-Hz modes via negative-ratio convention from STK).
11. Physics inside, macros outside (10 knobs over ~40 internal laws; industry-validated: Plaits=3, Imagine=4, Osmose=4+4).
12. Mode count = quality dial (shared global mode budget thins gracefully, importance-sorted culling — same as the 640-partial + 256-grain budgets).

Round robin: 4 oscs × per-osc seeds × AGE-scaled per-note re-rolls = layered round robins (rotation picks a different "player", jitter makes every strike unique) — exactly why this engine was saved for after rr1.

## 4. SAFETY / HEALERS (mandate compliance)

- **Schelleng auto-clamp** (bow cannot scratch), breath-window auto-clamp (winds always speak), lip-region clamp — the SWAM "Interactive Bow Compensation" pattern with real physics. AGE deliberately loosens the margins.
- State clamps every loop (±20 string, ±5 tube, ±1 hard), DC-blockers in every wind/bow/brass loop, denormal flush in tails, auto-sleep watchdog (|out|<0.001 for 0.1 s → sleep; STK Guitar law) = CPU healer.
- Subtract filter phase delay from EVERY loop length (STK law) + SVF phase compensation closed form `1−2·atan(2^(−c/12))/2π` — or it plays out of tune.
- Retune-safe resonators: 2D-rotation form only (direct-form biquads click on retune — verified).
- Mode-coefficient recompute per BLOCK, staggered (Rings: modes >24 every other block); never per-sample trig; never reseed per-block RNG (HARM lesson).
- Ring-tail declick at voice-free (house rule; modal tails are THE click machine).
- Viz: TIME-based watchdog + self-heal + breadcrumb (wd9 pattern), 60 Hz gate, buffer pre-init.

## 5. CPU BUDGET (Serum-bar)

Worst-case voice estimates @48k: SDL string pair ≈ 2×(delay read + 3 cheap filters) ≈ trivial (verified "order of one oscillator"); piano dispersion +4-20 biquads (low keys only, taper by key); MSW loop ≈ delay + table + 2 filters; modal 24-64 modes × 3-4 mul (Plaits runs 24 batched ×4 SIMD-friendly); body 6-8 biquads per voice (or per-osc sum for BOW — Maestre set once); commuted body = free. Global shared mode budget (e.g. 640 modes across all modal voices, mirroring the partial budget) with importance-sorted graceful thinning. Target: ≤3% core worst-case like HARM (2.8%). Benchmark before ship (hard rule).

## 6. UI — existing furniture ONLY

- Knob wrap: **.harm-knob-wrap clone verbatim** (pixel parity 187-225 law), 2 pages: ESSENTIALS / SCULPT — 5 knobs each, LFO-drag targets (KNOBDEST append).
- Header: **hm3 selector twins** — 'Grand · Concert' (FAMILY · FORM), regime-morph crossfaded (Wilt depth-into-fixed-time law: knobs MORPH, never switch/click).
- SRC pill: uni-pill clone (SUB Range/Form pattern). Menus: .samp-menu glass, no scrollbars.
- Viz: **.harm-view machinery reused** — white mode/partial bars (live, env-scaled, peak caps, −72 dB cull) + purple ghost waveform (scope tap); strike-position marker = one white dot on a thin string line (existing dot/notch identity). Thin-white/no-glass, purple select. Position-tracked insertion after identical anchors (replace-cascade law — 3rd-occurrence bug class).
- UI research note: Sculpture's celebrated material pad = 2 axes (stiffness × inner loss) — ours are the MATERIAL and STRETCH knobs; a future XY overlay on the viz could expose the same pad with zero new furniture (parked).

## 7. WIRING CHECKLIST (the traps, pre-listed)

- Engine enum: append `Engine::MODAL = 6` (IDs FROZEN, append-only); engine selector handles 7 choices (was 6 — verify UI menu + normalization /(N−1)).
- ~12 params/osc × 4 oscs (10 knobs + FAMILY + FORM + SRC): full 6-link chain each INCLUDING PluginEditor relay (silent no-op law) — model on HARM's 56-param precedent.
- KNOBDEST append for 10×4 LFO-drag targets AND **kDestInfo grows the same edit** (sc3 dead-annulus lesson).
- Mod-route persistence LAW (elForDest ∥ targets()), BinaryData cache-bust, build BOTH formats, manual install (space in bundle names), TERRAIN_BUILD bump, DAW reload for stale binary, headless-render verify (defaults show 0% in headless — verify via strings/C++).
- Grep old declick fixes before writing twin subsystems (rr2/Geode saturation-mute lesson).

## 8. BUILD ORDER (suggested)

1. Core (c): modal bank on 2D-rotation resonators + STK/Faust mode tables + MATERIAL/DECAY/POS/HARD → BARS·BELLS·SKIN sound day one.
2. Core (a): Rings-law SDL string + plucker + commuted bodies → PLUCK; + dispersion cascade + phantoms/soundboard → GRAND.
3. Core (b): bow (friction+Schelleng clamp+Maestre body) → flute → reed → brass.
4. BREATH/BLOOM/HALO/AGE cross-family; SRC=Sample; offline test harness per family (44-test HARM pattern) + pluginval s5 both + CPU bench.

## 9. TOP PORT LIST (license-cleared, ranked — file paths in research-1)

1. Rings/Plaits modal resonator + the 4 macro laws (MIT) 2. Rings KS string w/ dispersion + curved bridge (MIT) 3. STK Bowed + BowTable + Maestre body (MIT) 4. Elements exciter suite (mallet/plectrum/particles/flow/breath) (MIT) 5. STK Clarinet+Brass loops (MIT) 6. STK Flute + Faust blower (MIT) 7. BandedWG + Tibetan bowl pairs (MIT) 8. ModalBar 9-preset + Faust marimba-50 + churchBell-50 tables (MIT) 9. Rings plucker + Plaits Dust (MIT) 10. Rauhala-Välimäki closed-form dispersion (paper formulas) + Mathews-Smith 2D-rotation resonator (paper) + re-derived Surge decay-knee/audio-in-exciter concepts (GPL: concepts only).

## 10. KEY SOURCES

PASP (ccrma.stanford.edu/~jos/pasp) · Karjalainen/Välimäki/Tolonen CMJ 22(3) 1998 · MSW JASA 74(5) 1983 · Rauhala-Välimäki IEEE SPL 13(5) 2006 + DAFx-06 p_071 · Abel-Smith DAFx-06 p_013 · van den Doel-Pai (modal, 3-mul, material ρ) · Mathews-Smith SMAC03 · Bank-Sujbert JASA 117 (phantoms) · Weinreich JASA 62 · Woodhouse Acta Acustica 89 (friction/Schelleng) + euphonics.org · Almeida et al. JASA 134 (clarinet maps) · Benade 1973 (trumpet) · Rossing AST 22/25 (percussion tables) · Aramaki IEEE TASLP 19 (material law) · Klatzky-Pai (perception) · Bilbao 2009 (FDTD verdict) · manuals: Pianoteq/Chromaphone 3/Sculpture/SWAM v3.8/String Studio/Collision · STK · pichenettes/eurorack · DaisySP · faustlibraries.
