# RESEARCH STREAM 2 — COMMERCIAL PRODUCT TEARDOWNS (agent complete, verbatim notes)

## 1. MODARTT PIANOTEQ (realism gold standard)

**Architecture:** Nonlinear hammer model (felt compression, contact force) → per-string modal-style vibrating string model with real inharmonicity and (since v8) **dual polarization** (each string vibrates in two planes → beating + two-stage decay) → soundboard modeled as mechanical **impedance** (impedance sets sustain, cutoff+Q set high-partial decay) → radiation → virtual mic array (up to 5 mics, 20+ models, 360°, lid model) → FX. Sympathetic resonance couples all 200+ undamped strings; duplex scale modeled separately. All live 32-bit, up to 192kHz internal. Creator Philippe Guillaume = concert piano tuner + professor of applied mathematics — voiced by a tuner, not just derived. Pro tier: ~22-30 physical params editable **per individual note**.

**Params (name | range | effect | weight):**
- Hammer Hardness Piano/Mezzo/Forte | 3 values anchored ~vel 41/70/98 | velocity-indexed brightness spline, THE voicing control | HIGH
- Spectrum Profile | 8 sliders ±dB first 8 overtones | per-partial balance | MED
- Unison Width | 0→max detune of 3 strings/note | narrow=clean, wide=honky-tonk; also changes **direct sound duration** (detuned pair cancels → faster initial decay) | HIGH
- Octave Stretching | stretch factor | tuning curve vs inharmonicity | MED
- Direct Sound Duration | decay of initial segment | attack→sustain handoff speed | HIGH
- String Length | up to 10 m | sets inharmonicity; short = bell-like "upright" bass | HIGH
- Strike Point | position + **random humanization option** | comb-filters partials | HIGH
- Sympathetic Resonance | 0→max | undamped-string halo; SOS: pedal "bloom" = single most convincing realism feature | HIGH
- Duplex Scale Resonance | 0→max | shimmer from unstruck segments, treble brilliance | MED
- Blooming Energy/Inertia | amount + speed | energy transfer low→high overtones AFTER attack — attack keeps developing | MED
- Hammer Noise | 0→max | thump level, reads as mic proximity | HIGH
- Hammer Tone | woody↔bright | attack character independent of sustain | MED
- Soundboard Impedance | resistance | higher = longer sustain | HIGH
- Impedance Cutoff + Q | freq + slope | above cutoff partials die faster; "dullness vs sparkle" decay law | HIGH
- Damper Position | height/position | which overtones survive partial damping (half-pedal) | MED
- Damping Duration | efficiency | how fast released note dies | HIGH
- Damper Noise | 0→max | pedal whoosh | MED (huge for intimacy)
- Key Release Noise | 0→max | mechanical clunk | MED
- Condition | mint→wrecked + random seed | aging: per-note detune, darkening, inconsistency | HIGH (users: 0.2-0.4 = "real")
- Mic model/position/count, lid, width | 360°, 20+ models | capture fully separate from instrument | HIGH
- Velocity curve + Dynamics (60 dB) | user spline | controller→model input; Dynamics compresses pp↔ff spectral spread | HIGH

**Secret sauce:** (1) Velocity-indexed nonlinearity everywhere — continuous timbre-vs-force law is why runs/crescendos sound real (sampling only crossfades). (2) **Dual polarization** (v8) fixed "modeled piano sounds thin" — two orthogonal planes, slightly different freq/decay → beating + two-stage decay. (3) **Imperfection is a parameter** — users chasing realism INCREASE imperfection (condition 0.2-0.4) and fix the CAPTURE (close mics, width 0.12-0.40, kill reverb, tame top 2 octaves). (4) Giveaway zones: high-treble energy + unnatural sustain lengths read synthetic first.

## 2. AAS CHROMAPHONE 3

**Architecture:** (Mallet + Noise sources, own level/direct) → Resonators A/B, **Parallel** (both excited, Balance mixes) or **Coupled** (only A excited; energy flows BIDIRECTIONALLY; Balance = relative mechanical **impedance ratio** — coupling changes tone AND decay of both, like drumhead bolted to shell). No samples. Two engines can layer.

**Params:** Mallet: Level, Stiffness (harder = narrower impulse = brighter), Noise, Color (HP on noise), Key/Vel mod. Noise src: envelope (AHDSR), filter type (res LP/HP/BP/LP+HP/10-band graphic), Freq/Q/Width, **Density** (random-impulse rate: crackle→smooth), S&H. Resonator A/B: Type = **String, Beam, Marimba, Plate, Drumhead, Membrane, Open Tube, Closed Tube, Manual (4 partials)**; Pitch (semis+cents); Key Track 0→1.00:1 (<1 = per-key timbre drift like real percussion); Decay; Release (% of decay = damper efficiency); **Material −1..+1** (freq-dependent damping tilt: −1 wood/felt, +1 glass/metal); **Tone** (dB/oct static tilt; −6 dB/oct = 1/f natural); Radius (tubes: smaller = brighter/shorter); Low Cut (24dB/oct, harmonic-series steps); Hit Position (0-100%, mod vel/pitch/RANDOM); **Mode Density Low/Med/High/Full = 4/16/30/70 modes** (CPU↔richness; sparse = "cheap FM-ish" timbre). Coupling: Off/On + Balance.

**Secret sauce:** (1) Coupled resonators = complex non-exponential decays of assembled instruments (what Collision lacks). (2) Material (damping-vs-freq) SEPARATED from Tone (static tilt) — together they nail "what it's made of" vs "where you EQ'd it". (3) Noise source with own env+filter = bowed/blown territory; source duration, not resonator, defines strike vs sustain.

## 3. LOGIC SCULPTURE

**Architecture:** One modeled string (morphable), up to **3 Objects** (excite/disturb/damp) → 2 movable Pickups → amp env → Waveshaper → filter → **Body EQ** (modeled bodies) → delay → limiter. Morph pad (5-point). String state PERSISTS between notes (retriggers interact with residual vibration).

**String params:** Material pad X = **Stiffness** (nylon→steel→solid-bar; inharmonicity) | HIGH. Y = **Inner Loss** (freq-DEPENDENT damping — mellows as it rings) | HIGH. Corners: lowS+lowL=steel; highS+lowL=bell/glass; lowS+highL=nylon; highS+highL=wood. **Media Loss** = freq-INDEPENDENT damping (air/water), keyscale+release variants | HIGH. **Tension Mod** = momentary upward detune when excursion large — pluck/sitar attack pitch-flare | HIGH (signature realism trick). Resolution = number of computed harmonics (low = inharmonic even at stiffness 0; CPU dial) | MED. Keyscale/Release per-param (above/below C3 + release values) | HIGH.

**Objects** (Obj1 excite; Obj2 excite/disturb + External; Obj3 disturb/damp; each: Gate KeyOn/Always/KeyOff, Strength, Timbre ±, Variation, VeloSens, POSITION on string):
- Impulse (amp/width/vel-width) · Strike (speed/mass/felt stiffness) · **Gravity Strike** (falls back → multiple contacts; can retrigger at note-off) · Pick (force-speed/ratio/plectrum stiffness) · **Bow** (speed/pressure/slip-stick char) · Bow Wide · Noise (level/BW/res) · **Blow** (lip clearance/pressure/noisiness) · External (sidechain audio!) · Disturb (hardness/distance/width) · Disturb 2-sided (ring limiting excursion) · **Bouncing** (loose rattling object, random) · **Bound** (fingerboard buzz: distance/slope/reflection) · Mass (attached → inharmonic) · Damp (localized felt).

**Output:** Pickup A/B 0-1 (comb), B phase invert, Key Spread + Pickup Spread (stereo). Waveshaper (VariDrive/SoftSat/Tube/Scream). Body EQ: modeled guitar/violin/cello/flute bodies + **Formant Intensity** (0=flat→1=strong), **Formant Shift** (log ×10 = resize body), **Formant Stretch** (reshape body), Fine Structure.

**Secret sauce:** (1) TWO orthogonal loss types (inner=freq-dep, media=freq-indep) + stiffness = 3 numbers describe any material. (2) Tension Mod = the perceptually loudest pluck nonlinearity in one slider. (3) Realism from INTERACTION of objects (Bow + Bound fingerboard + Damp = playable cello), not any single element.

## 4. AUDIO MODELING SWAM

**Architecture:** exciter (nonlinear reed/lip/bow) → resonator (digital waveguide, J.O. Smith lineage) → radiator (convolved measured body/bell) + **behavioral modeling layer**: encodes how a PLAYER produces transitions (legato finger paths, bow changes, breath attacks), applies instrument-correct couplings automatically. RAM 30-52 MB, no samples.

**Core doctrine:** NO sound without continuous Expression stream (CC11) — "like a saxophone needs air." UI paints Expression RED above 75% — realism lives in mp-mf middle. Velocity ≠ attack on strings (velocity → bow-pressure accent).

**Solo Strings v3.8 key params:** Expression (bow speed→dynamics), Vibrato Depth/Rate, Bow Pressure (flautando→scratch), Bow Position (ponticello↔tasto), Pressure Accent, Attack Ramp Speed; Alternate Fingering (Mid/Near Bridge/Near Nut+Open), Harmonics (flageolets); Play Mode (Bow/Pizz/Col Legno), Gesture Mode (Expression/Bipolar/Bowing-displacement), Bow Start Down/Up, Bow Lift (Off/On String — release rings vs stops), Tremolo modes; Bow Polyphony modes + CrossString Muting (whether old string rings across transition); Instrument Body, Sordino, **Rosin**, **Bow Noise**, Timbral Correction + Harmonic A/B gain, **String Resonance**, **Open Strings** (sympathetic), EQ; microtuning + temperament spread; **String Model: Real (4 strings) vs Virtual Adaptive Resizing (one continuously-resizing string → 48-st seamless bends)**; Advanced Legato (position-shift modeled), PortamSplit; **Vibrato Rate Randomness (default 6%)**, Vibrato Fade-In (250 ms default), **Random Bow Amount** (pressure+speed+position), **Random Finger**, Dynamic Transitions, Panpot Dyn1/Dyn2 (**emulates small player movements**), **Interactive Bow Compensation** (auto-corrects pressure to stay in good vibrating regime!), Staccato Interval ~18 ms; MPE; Breath Ctrl re-attack; Ambiente room w/ physical Source Delay.

**Winds/Brass:** Breath Intensity, Attack Tongue, Flutter, **Growl**, **SubHarmonic**, Breath Noise, Formant (resize player/instrument), Overblow trigger on fast dynamics, Half-valve, Bell Resonance, Dirtiness, mutes.

**Secret sauce:** (1) Sound = f(gesture stream) not f(note event); velocity demoted. (2) Behavioral layer = the moat: picks string/finger/bow direction, injects BOUNDED human noise (6% vibrato jitter, random bow/finger). (3) **Self-healing physics** — Interactive Bow Compensation bends naive input back into the physical regime.

## 5. AAS STRING STUDIO VS-3

Exciter (Plectrum: Protrusion/Stiffness/Damping/Velocity · Hammer×2 incl BOUNCING: Mass/Stiffness/Velocity/Damping · Bow: Force/Friction/Velocity · Magnetic pickup-as-driver) → String (Decay, Damping HF loss, **Inharm**, Release = ratio held:released decay, Level — all key-tracked) + Geometry (Exciter Pos 0-0.5, Damper Pos 0-0.5, **Abs on/off** = fixed vs scales with note; real instruments = Abs ON) + Damper as MECHANICAL OBJECT (Mass/Stiffness/Velocity/Damping/Gated) + Termination (Finger Mass/Stiffness, Fret Stiffness = attack pitch/buzz) → Filter (incl **Formant a-e-i-o-u**) → Body (**5 types × 5 sizes Tiny→Huge**, Decay, cuts, Mix) → Distortion → FX. Vibrato engine-level (Rate 0.3-10 Hz, Amount, Delay, Fade). Unison 2/4.

**Secret sauce:** (1) Damper = mechanical felt object, not VCA release. (2) Positions scale-with-note vs fixed (why guitars brighten up the neck). (3) Pickup-as-exciter + body-off = correct electric-guitar path — model the CAPTURE distinctly from the INSTRUMENT.

## 6. ABLETON COLLISION / AAS OBJEQ

Collision: Mallet (Volume, Stiffness, Noise "chiff", Color, pitch/vel mod) + Noise (filter + ADSR + mods) → Res 1 & 2 **Serial or Parallel**. Types: **Beam, Marimba (retuned beam), String, Membrane, Plate, Pipe (open end), Tube (closed)**. Per-res: Decay; **Material** (damping-vs-freq tilt); **Radius** (pipe/tube); **Brightness** (amp tilt); **Inharmonics ±** (compress↔stretch partials = bell↔string morph); **Ratio** (membrane/plate aspect); **Opening** (pipe closed↔open); **Hit Position**; **Listening Position L/R** (separate pickup per stereo side = instant believable stereo); Tune/Fine/Pitch-env.
Objeq Delay 2: string/plate/drumhead/beam; Frequency, Decay, Material, **Formant (= hit position)**, Polarity — a usable resonator ships with 4-5 knobs.

## 7. ARTURIA + EXPRESSIVE E

- MicroFreak ships **Plaits Modal** as 3 knobs (Harmonics=inharmonicity/material, Timbre=exciter brightness, Morph=decay) + Karplus engine. Lesson: a GOOD 3-macro projection is musically sufficient.
- **Pigments 6 (2025, retained in 7)** Modal engine: 2 flavors **String** (soft) and **Beam** (clicky), decay, **brilliance**, timbre variants (nylon, hollow); **dual exciter**: **Collision** exciter (algorithm | transient sample library | external audio-in) + continuous **Friction** exciter (algorithm | colored noise | audio-in | **granular** — granulated rain exciting the resonator). Reviewers: "organic, acoustic-like sounds impossible with VA/wavetable."
- Expressive E NOISY (noise+resonance, MPE-first); **Imagine** (with AAS): two-layer over skins/strings/bars/tubes; per-excitator only ~4 params: **Shine** (brightness), **Mute** (damping), **Position**, **Impact**. SOS: "remarkable just how few parameters." Osmose/EaganMatrix: 4+4 macros over deep guts. Industry converges: **physics inside, macros outside.**

## 8. LANDMARKS

- **Yamaha VL1 (1994):** controller list IS the instrument: **Pressure** (volume AND timbre), **Embouchure** (pitch AND timbre, deliberately coupled), Tonguing, Vibrato (via pitch + embouchure), **Scream** (chaotic overdriven-air), Breath Noise, **Growl** (LF pressure mod), **Throat** (player formant), Damping+Absorption. Lesson: expose PLAYER variables, couple them the way physics couples them.
- **Korg Prophecy/Z1:** 13 osc algorithms mixing PM+VA in one subtractive voice (brass lip, reed, plucked w/ pick type/condition/position, bowed w/ scrape, comb, resonance). Lesson: PM exciters as interchangeable oscillator types = practical hybrid, polyphonic in Z1.
- **Roland V-Piano:** Unison Tune, Hammer Hardness, **Cross Resonance**, Tone Color, String/Damper/Soundboard/Key-off Resonance (FOUR separate resonance sends), Damping Time, Damper Noise; "All Silver" fantasy preset. Lesson: separately-addressable resonance subsystems + physically-parameterized FANTASY as a headline feature.
- **GeoShred/moForte (J.O. Smith, FAUST):** 6 independent strings (nylon→steel continuum), 3 body resonances, 2 pickups, **guitar/sitar/tambura bridge models as a type switch**, pick position, harmonics, solid/hollow, fret-scrape, **modeled amp feedback**. Believable only under continuous per-finger control.

---

# CROSS-PRODUCT SYNTHESIS

## (a) Essential core — in EVERY serious PM product (~13)
1. **Exciter type** (strike/pick/bow/blow/noise)
2. **Exciter hardness/stiffness** (universal brightness-of-attack law)
3. **Excitation position** (cheapest huge timbre lever)
4. **Exciter noise + color** (always a separate signal w/ own filter/level)
5. **Decay / global damping**
6. **Frequency-dependent damping = "Material"** (wood/metal/glass axis, distinct from #5)
7. **Inharmonicity/stiffness** (bell↔string continuum)
8. **Brightness/spectral tilt** (static, distinct from damping)
9. **Keytracking of pitch AND decay AND timbre** (uniformity across keyboard = giveaway)
10. **Velocity→exciter mapping** (continuous, not layered)
11. **Release/damper model** (mechanical event, never a VCA ramp)
12. **Sympathetic/coupled resonance**
13. **Output/capture stage separate from instrument** (body/mic/pickup)

## (b) Differentiators / sculpt candidates
- **Resonator type/geometry switch** (string/beam/marimba/plate/membrane/drumhead/open tube/closed tube) — biggest timbre selector
- **Inharmonics as continuous ± morph** (compress↔stretch) — most sound-designy knob
- **Material pad as 2D surface** (stiffness × inner loss) — most elegant UI in the field
- **Hit/listening position pair** (excitation + pickup, per stereo side)
- **Coupling balance/impedance ratio** between two resonators; serial/parallel/coupled
- **Tube Radius + Opening; plate/membrane Ratio**
- **Spectrum Profile** (first-8-partials gains)
- **Blooming energy/inertia** (post-attack energy migration up — reads "alive", nobody else has it)
- **Tension Mod** (nonlinear attack detune — instant pluck realism, sitar at extremes)
- **Condition/age + random seed** (one macro for accumulated imperfection)
- **Body formant Shift/Stretch/Intensity; body Size**
- **Unison width / dual polarization** (two-stage decay, beating)

## (c) Realism lessons (repeat across ALL products)
1. **Noise is half the instrument** (hammer/chiff/bow/rosin/breath/damper/key noises convince reviewers, not the tone).
2. **Two damping laws, never one** (freq-independent + freq-dependent; their ratio = readable material; single decay knob = #1 tell of a fake).
3. **Nonlinearity concentrated at the attack** (felt, tension flare, gravity re-strikes, overblow; budget the nonlinearity in the first 50 ms) + slow post-attack blooming.
4. **Energy must go somewhere and come back** (coupling, sympathetic, key-off resonance, persistent string state, feedback; one-way chains sound dead).
5. **Randomness bounded and physical, with a seed** (per-note re-rolls of physical quantities read human; free-running mod reads chorus).
6. **For sustained instruments the continuous control stream IS the product** (SWAM refuses to sound without CC11; realism lives in restrained middle dynamics — red warning above 75%).
7. **Release is a modeled mechanical event** (damper mass/position, bow lift, key-off resonance).
8. **Detuned pairs + dual polarization make sustain "3D"** (cheapest big realism win).
9. **Separate instrument from capture** (users fix mics/width/reverb before physics).
10. **Keyscale everything** (per-note edits, above/below-C3 splits, key-tracked decay/timbre).
11. **Physics inside, macros outside** (3-6 behavioral macros for players; guardrails bend bad input back into sweet regime — Interactive Bow Compensation).
12. **Mode count is a quality dial you can expose** (4/16/30/70; reduced resolution doubles as lo-fi timbre; CPU-budget-that-thins-gracefully compatible).

Sources: modartt.com manuals/features + forum id=7777 · SOS Pianoteq review · applied-acoustics.com chromaphone-3 + string-studio-vs-3 + objeq manuals · help.apple.com logicpro ch.14 · SWAM Solo Strings v3.8 manual PDF · audiomodeling.com blog · ableton.com live-manual/12 Collision · support.arturia.com Pigments 6 · SOS reviews: Imagine, Osmose, VL1, Korg Z1, V-Piano, MicroFreak · moforte.com · pianobuyer.com V-Piano.
