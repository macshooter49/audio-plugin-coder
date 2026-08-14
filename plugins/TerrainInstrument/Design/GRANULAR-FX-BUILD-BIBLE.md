# Terrain Instrument — Granular FX Build Bible

**v1 — research complete. The single authoritative spec for the 4th FX device.**
Written after Phase G (distortion certified, fb345). Companion to `DISTORTION-BUILD-BIBLE.md` /
`REVERB-BUILD-BIBLE.md` — same structure, same laws, same chassis. Every C++ line reference below
was read in this repo on 2026-08-14; every external claim carries a URL in §15.

> **RESEARCH NOTE.** The session's WebSearch budget was exhausted by parallel researchers before this
> run started; research proceeded by direct WebFetch of primary sources (Mutable Clouds + Beads
> manuals, the Clouds firmware source, Arturia's Efx Fragments product pages + the SOS review,
> Soundtoys Crystallizer, the MusicRadar Portal review, Argotlunar's repo, KVR's Emergence page) plus
> a full repo recon. Ableton Grain Delay numbers are from the Live manual (URL in §15) and should be
> spot-checked before quoting in marketing copy. ⚠️ **`index.html` line numbers drift with every UI
> build — re-grep the symbol, don't trust the number.** C++ refs have been stable.

---

## 0. The scope decision

**ONE device, named `Granular`.** The fourth FX-rack flagship after Reverb, Delay, Distortion.

### Why this device is the headline

**Serum 2 has NO granular FX.** Its full FX menu (Max's screenshot, 2026-08): Bode, Chorus,
Compressor, Convolve, Delay, Distortion, Equalizer, Filter, Flanger, Hyper/Dimension, Phaser,
Reverb, Splitter L/H, Splitter L/M/H, Splitter M/S, Utility. Vital: none. Phase Plant: none stock.
The commercial comps are **standalone paid plugins**: Arturia Efx Fragments (€99 — the price of our
whole synth), Output Portal (~$149), Soundtoys Crystallizer ($99). Shipping a certified granular FX
**inside** the rack is a checkbox none of the competitors at our price point have. This is the one
device where we are not chasing the bar — we ARE the bar.

### Why it is cheap for us and expensive for everyone else

Terrain already ships **two granulators**:

1. `tw::GranularEngine` (Source/GranularEngine.h, 765 lines) — the per-osc granular **oscillator**:
   64-grain pool, compact active-list, three RMS-matched windows, key-quantized pitch scatter,
   1/√overlap normalization with 3 ms declick glide, a UI scatter follower (`cloudSnapshot`). It is
   the most heavily optimized engine in the repo (grain-major `renderBlockAdd`, skew LUT, shared
   static window tables).
2. `GrainEngine` (Source/GrainEngine.h, 395 lines) — the OLD **live-input** granulator (held as
   `grainEngineL/R`, PluginProcessor.h:1488-1489; chain comment "Input → GrainEngine → GrainFilter →
   TapeProcessor → TapeLoop" at PluginProcessor.cpp:6646). It already proves the two things the FX
   device needs: a **circular capture buffer** written every sample (5 s, GrainEngine.h:58,64-66) and
   a **continuous freeze** implemented as a write-blend
   (`buf[w] = in·(1−freeze) + buf[w]·freeze`, GrainEngine.h:112-115).

The build is therefore a **marriage, not an invention**: GranularEngine's grain kernel + GrainEngine's
capture-ring idiom + the locked fb275 chassis + the DelayEngine's sync/glide grammar.

**Max's layout mandate (given to this research):** the FX card front is the **exact front-page
granular layout** — the sample-view waveform shell + the two-page 6-knob rows — sized to the rack
with the standard device header/footer. §6 maps this; the fb275 reconciliation is Open Question #1.

---

## 1. History and circuits — the three granular lineages

Granular processing has one theory and three product cultures. Every proposed Type in §4 descends
from a named branch.

### The theory
- **Dennis Gabor, 1946-47** — "acoustical quanta": any signal is decomposable into elementary
  Gaussian-windowed grains (the acoustic uncertainty principle). The window IS the physics: its
  shape trades time-smear against frequency-smear.
- **Iannis Xenakis, 1959-60** (*Analogique A/B*) — first compositional granulation, tape splices.
- **Curtis Roads, 1974→** (*Microsound*, MIT Press 2001) — the digital canon: grain = windowed
  sonic event of 1-100 ms; density/duration/pitch/spatial scatter as independent stochastic axes.
- **Barry Truax, 1986** (GSX/PODX) — first **real-time** granulation of sampled and live sound —
  the direct ancestor of a granular *FX device* (grains from a live stream, not a loaded file).

### Branch A — the pitch-splice delay (1970s hardware → Crystallizer, Grain Delay)
A delay-line pitch shifter IS a two-grain granulator: two read heads on a ring, delay ramping at
rate (1−ratio), crossfaded — the Eventide H910/H949/H3000 architecture. The H3000's *Crystal
Echoes* preset (reverse splice + pitch + long feedback) became **Soundtoys Crystallizer**, whose
designers wrote algorithms for the original H3000; its charm is explicitly the **artifact**: "the
original old school devices used a simpler resample and crossfade technique that introduced audible
artifacts… an essential part of Crystallizer's 80s-futuristic charm" (soundtoys.com). Params:
Splice (grain size, tempo-syncable), Pitch, Delay, **Recycle** (feedback *through the shifter* —
each pass shifts again = the ascending spiral), Gate/Duck, splice **direction** (reverse).
**Ableton Grain Delay** (Live 1.5→) is the minimal statement of the same branch: Frequency (spawn
rate ~1-150 Hz), Pitch, **Spray** (position jitter, 0-500 ms), Random Pitch, Feedback, Dry/Wet on
an XY pad. One monolithic sound, loved for 25 years.

### Branch B — the texture cloud (Roads → Clouds/Beads → Portal/Fragments/Emergence)
- **Mutable Instruments Clouds** (2014, open source): Position (back in time through the buffer),
  Size, Pitch (V/oct), **Density** (bipolar: CCW = constant-rate spawning, 12 o'clock = none, CW =
  random/Poisson spawning, overlap grows toward the ends), **Texture** (window morph "square
  (boxcar), triangle, and then Hann window", past 2 o'clock a diffuser), Blend axis (dry/wet /
  stereo spread / feedback / reverb), **Freeze** ("stops incoming audio recording… granularization
  continues from the captured buffer"), quality tiers down to **8-bit µ-law** — "sounds like a
  Cassette, or a Fairlight — less hiss, more distortion". 40-60 simultaneous grains. Source
  (github.com/pichenettes/eurorack, clouds/dsp/granular_processor.h): four `PlaybackMode`s —
  GRANULAR, STRETCH (WSOLA), LOOPING_DELAY, SPECTRAL — over one shared ring, 32 kHz, mono ring =
  2× the seconds of the stereo ring, µ-law = 2× again.
- **Mutable Beads** (2021): the refinement — TIME (newest→oldest audio), SIZE bipolar (CW longer,
  **CCW reversed grains**, fully CW = a single infinite grain = delay mode), SHAPE ("fully CCW
  creates clicky, rectangular envelopes… fully CW slow attacks reminiscent of reversed grains"),
  DENSITY bipolar (constant vs randomly-modulated rate, **reaching audio rate at the extremes** —
  granular fuses into AM/formant synthesis), quality as **medium emulation**: "cold digital / sunny
  tape / scorched cassette (wow and flutter)". Feedback with per-quality limiting schemes.
- **Clouds Parasites** (community firmware): asymmetric windows (square/ramp-up/ramp-down/
  triangle), smaller grains, reverse toggle — proof users want window asymmetry (our Skew knob
  already does this).
- **Output Portal** (2019): the cloud branch productized — grain rate syncable "1/64t to 1 bar",
  pitch "snapped to a range of scales and chords", per-grain pan, instant **Reverse**, the XY
  macro dial, 7 post-FX. - **Arturia Efx Fragments** (2022, €99) — the closest commercial comp:
  buffer "one eighth of a bar to four bars", three release modes (**Classic / Rhythmic / Texture**
  — Rhythmic = tempo-synced spawn clock, Texture = dense layered clouds with a Layers macro), grain
  capture Speed/Offset/Manual-Scan with **transient-detect quantize**, pitch ±3 oct **quantized to
  15 scales**, "Grain Crush" (CMI/Emulator II bit-crush on grains), freeze, double-axis
  spatializer, env follower + 3 function generators + step sequencer. UI: "buffer waveform display
  (white = record head, yellow = playback)".
- **Emergence** (Daniel Gergely, free/$20): up to **600 grains** in 4 streams; KVR users love it
  precisely for emergent unpredictability. The lesson: grain count IS the drama axis.

### Branch C — the freeze/stretch (Paulstretch → Clouds STRETCH → "living freeze")
Extreme time-stretch as an aesthetic: Paulstretch (Paul Nasca, 2006) made 50× stretch a genre.
Clouds' STRETCH mode is WSOLA on the ring. Our own oscillator engine already ships the "**living
freeze**" (GranularEngine.h:11-12: "Scan=0 freezes (grains keep spraying from the held slice)") —
frozen ≠ static, the cloud keeps breathing. This is the branch Serum 2 cannot touch at all.

---

## 2. Repo recon — what exists, knob by knob (STEP 1 of the mandate)

### 2.1 The front-page granular UI (the layout the FX card must clone)

The granular oscillator view = the **sample-view shell** with only the knob row swapped
(index.html:4273-4276: `.engine-granular:not(.swapped) .sample-view { display:flex }`; the comment
at :4273 says exactly this). Shell contents (per-osc, osc A anchors):

| Shell element | What it is | UI anchor (osc A) |
|---|---|---|
| waveform canvas | the loaded-sample draw + region | inside `.sample-view` (redraw via `__terrainSampleRedraw`, :12378) |
| `.samp-h s / e` | region Start/End drag handles | :5899-5900 |
| `.samp-h ls / le` | LOOP bracket handles (hidden when loop off, :4454) | :5901-5902 |
| `.fade-sq fi/fo`, `.fade-cv fi/fo` | fade handles — **hidden for granular** (:4513) | :5903-5906 |
| `.samp-drop` | "＋ Drop sample here" target | :5907 |
| `.samp-exp` | chop-editor expand glyph | :5908 |
| `.samp-ph` follower | the plain white playhead line — granular reuses it as the scan follower (:4486 comment) | CSS :4486-4534 |
| `.samp-head` | name strip (granular variant, :4534); preset-wrap hidden :4535; wt/samp nav hidden :4563/:4597 | |

**The two-page knob rows** (`.gran-knob-wrap`, CSS :4393-4407 — 6-column grid :4398, page flip
classes :4399-4401, `.gk-arrow` chevron :4407; page-flip JS :13790-13795; readout formatting
:13771). HTML per osc: **A :5946-5966 · B :6218-6238 · C :6509-6529 · D :6781-6801.**

| Page | Knob label | data-syn (osc A) | Engine param (GranularEngine.h) | Range / map |
|---|---|---|---|---|
| 1 | Scan | `SYN_OSC_A_GRAIN_SCAN` | `scan` :38 | −1..+1, ×2 in tick → ±200%; 0 = freeze; <0 reverse |
| 1 | Density | `SYN_OSC_A_GRAIN_DENSITY` | `density` :40 | 0..1 → 1..220 grains/s, log (`pow(220,d)`, :421) |
| 1 | Size | `SYN_OSC_A_GRAIN_SIZE` | `size` :41 | 0..1 → 2..900 ms, log (`0.002·pow(450,s)`, :424) |
| 1 | Spray | `SYN_OSC_A_GRAIN_SPRAY` | `spray` :42 | 0..1 → birth jitter up to ±half the region (:519) |
| 1 | Shape | `SYN_OSC_A_GRAIN_SHAPE` | `shape` :43 | 0..1 window morph Flat-top→Hann→Bell (:566-607) |
| 1 | Key | `SYN_OSC_A_GRAIN_KEY` | `key` :49 | choice 0..6 Off/Oct/5th/Chord/Maj/Min/Penta (:695-712) |
| 2 | Position | `SYN_OSC_A_GRAIN_POSITION` | `position` :39 | 0..1 birth anchor in region |
| 2 | Pitch | `SYN_OSC_A_GRAIN_PITCH` | `pitch` :45 | ±24 st base transpose |
| 2 | P.Spray | `SYN_OSC_A_GRAIN_PSPRAY` | `pitchSpray` :46 | 0..1 → ±24 st per-grain scatter (:534) |
| 2 | Width | `SYN_OSC_A_GRAIN_WIDTH` | `width` :47 | 0..1 per-grain equal-power pan spread (:555-558) |
| 2 | Dir | `SYN_OSC_A_GRAIN_DIR` | `dir` :48 | −1..+1 all-rev / coin-flip / all-fwd (:539-542) |
| 2 | Skew | `SYN_OSC_A_GRAIN_SKEW` | `skew` :44 | −1..+1 window asymmetry pluck↔swell (LUT :626-637) |

Waveform **right-click extras** (menu append :12838-12845, sliders :12965-12967): Stretch 0..1 +
Stretch Mode (Tones/Beats/Texture) + Air 0..1 — engine params :52-54, stretch = real head-slowdown
time-stretch `1/(1+3s)` (:422, :460-464). Param IDs: ParameterIDs.hpp:741-765 (page-1 six ×4 oscs),
:803-828 (page-2 six ×4 oscs). The tape "Feed loop into granular" button exists at :5497
(`.tape-feed-btn #btn-feed`) — precedent for feeding a live loop INTO the granular path.

### 2.2 `tw::GranularEngine` — the kernel we recycle (all verified by read)

- **Pool**: 64 grains (`kPool` :353), compact active/free index lists (:729-733) — the 72 %-cost
  full-pool scan is already killed (:169-172 comment).
- **Spawner**: async jittered countdown — `interval = fs/densHz`, jitter ±50 % of interval,
  re-arm `countdown_ += interval + jit` (:158-167). This IS Clouds' "random sowing".
- **Windows**: Flat-top(Tukey α=0.12)/Hann/Bell(Gaussian σ=0.16·Hann) LUTs, kWin=2048, **RMS-matched
  to Hann so Shape sweeps timbre not loudness** (:571-605), shared static across every instance
  (:570 comment: was 140 MB of duplicates). Zero at both ends ⇒ grain births/deaths cannot click.
- **Normalization**: `norm = 1/√max(1, density·grainSec)` per block (:427), glided ~3 ms
  (`normAlpha_`, :88) because raw per-block steps clicked the whole cloud (:198-203). **This is the
  unity-gain law of §8** — overlap-independent output RMS.
- **Per-grain state capture**: bounds (:524-527), pitch (:532-536), direction (:539-542), pan
  (:555-558) are latched at spawn; `reflectAtBounds` (:378-392) reflects instead of fmod-teleporting
  — the documented fix for Max's "fire crackle".
- **Key quantize**: `snapToKey` + `randKeyOffset` with octave weighting {−12,0,0,0,12,12,12,24}
  (:683-724) — the in-key shimmer, **audible even at P.Spray 0** (:716-718 comment).
- **Interpolation**: 4-point Hermite (:662-673), clamped `samp0/samp1` edge reads (:660-661).
- **Grain-major block render**: `renderBlockAdd` (:235-323), kChunk=128 stack scratch, bit-exact
  vs `tick()` (harness-asserted, :234).
- **Global grain budget hook**: `setGrainBudget(int* used, int cap)` (:97) — processor-owned,
  audio-thread-only. The FX device MUST join this budget.
- **The blocker for FX use**: `setSample` takes a **static borrowed view** (`const float* const*`,
  fixed `numSamples_`, :99-105); `readPos` is an absolute index into a non-moving buffer; region/
  loop bounds are 0..1 of that fixed span. **It cannot read a moving ring as-is** — §3 designs the
  adaptation.

### 2.3 `GrainEngine` — the live-input precedent (verified by read)

- Circular buffer per channel, `BUFFER_SECONDS = 5.0` (:58), write head wraps `% bufferLength`
  (:117), **freeze = continuous write-blend** `in·(1−f) + buf·f` (:112-115) — freeze is an
  *amount*, not a switch: partial freeze = regenerative smear. Steal this law.
- Wander V3 per-grain dice (:41-52 header comment): reverse p=i·0.5, half-speed p=i·0.3, stutter
  30-120 ms micro-loop p=i·0.3, dropout p=i·0.2, per-grain tanh sat p=i·0.35, timing scatter
  p=i·0.4, where i=wander². **This is a finished "Worn/Pulverize" character** — steal the table.
- NaN guard + soft-clip on the wet sum (:195-200) — keep both.
- Limitations vs the oscillator engine: Hann-only, linear interp, mono per instance, full-pool
  scan. The FX device recycles its *ideas*, not its kernel.

### 2.4 `RollingCaptureBuffer` — the lock-free ring pattern (verified by read)

Audio thread sole writer, atomics with release/acquire, **`prepare` preserves contents when the
sample rate is unchanged** (:27-30 — "DAW may call prepareToPlay multiple times… don't wipe
captured audio"). The FX capture ring copies this idiom (but needs no atomics: write and read both
live on the audio thread).

### 2.5 The FX chassis grammar (what the 4th device plugs into)

- Param families: `SYN_RVB_*` (ParameterIDs.hpp:345-369), `SYN_DLY_*` (:374-401), `SYN_DST_*`
  (:406+). Pattern per device: `_TYPE`, `_CHARACTER`, front knobs, back knobs, `_SRC_A..D/SUB/
  NOISE` route bools, `_POWER`, per-device pills (`_FREEZE` :367, `_SYNC`/`_PING` :395-396,
  `_HQ` :398). → ours: **`SYN_GRN_*`**.
- Engines held one-each: `DelayEngine delayEngine;` / `tw::DistortionEngine distortionEngine;`
  (PluginProcessor.h:1548-1549) → add `tw::GranularFxEngine granularFxEngine;`.
- **Bus level**: `kVoiceToFxPad = 0.5f` (−6 dB) applied at PluginProcessor.cpp:6300-6301; measured
  program on the FX bus ≈ **−26 dBFS** (the distortion bible's root-cause measurement). Every
  threshold in this file is stated relative to that.
- ⚠️ **THE fb305/fb338 LANDMINE — a 4th send bus re-breaks the main-send exclusion unless three
  exact lines are edited.** The law (fb338 comment, verbatim in-tree): "EVERY send bus joins EVERY
  main-send exclusion." The three sums currently read
  `(rvbSendL + dlySendL + dstSendL) · outputGain · kVoiceToFxPad` at **PluginProcessor.cpp:7159,
  :7326, :7357-7358** (reverb block, distortion block, delay block). Adding `grnSendL/R` means a
  **fourth copy of the sum appears in the new granular block AND `+ grnSendL[i]` is added to all
  three existing lines** (and the R twins). Miss one and a per-osc-routed osc's dry re-enters the
  main mix — the fb305 double-dry bug, again.
- DelayEngine's allocation precedent for long sync ranges: `16.5 s` per channel for the 4-bar
  ceiling (DelayEngine.h:41 "fb304 — up to ~16 s (4 bars @ 60 BPM)").
- Delay's tempo resolver ("resolved to ms by the host processor", DelayEngine.h:4-5) is the sync
  grammar to reuse for the Rate knob's synced divisions.

---

## 3. The capture ring — how an FX granular grabs the LIVE bus (STEP 2 of the mandate)

### 3.1 Why GranularEngine can't read a ring directly (measured against the source)

Three structural assumptions break: (1) `readPos` is an absolute index into a buffer whose
`numSamples_` never moves (:99-105, :522); (2) grain bounds are latched at spawn and *reflected* at
(:378-392) — on a ring the "bounds" slide one sample per sample; (3) `samp0/samp1` clamp at the
buffer edges (:660-661) — a ring has no edges, it has a **write head**, and reading across it is
the one true glitch. A "linearize the window per block" copy-out wrapper is out: 16.5 s × 48 k ×
2 ch × 4 B ≈ 6.3 MB memcpy'd per block. Dead on arrival.

### 3.2 The design: `GranularFxEngine` — ring-relative addressing

Fork the kernel (≈300 lines of GranularEngine survive verbatim: spawner, windows, norm, key
tables, pan, active-list) and change ONE representation: **a grain stores its read position as an
AGE (samples behind the write head), not an index.**

```
ring:      float ringL[N], ringR[N];  N = 2^ceil(log2(16.5·fs))   (mask addressing, DelayEngine idiom)
write:     ringL[w & mask] = inL·(1−freezeSm) + ringL[w & mask]·freezeSm;  ++w;   (GrainEngine.h:112 law)
grain:     double age;      // samples behind w at the CURRENT sample; age ∈ [aMin, aMax]
           double ageInc = 1.0 − ratio;   // per output sample: write head moves +1, read moves +ratio
read:      readHermite(w − age)           // 4-tap Hermite on the ring, mask-wrapped, no clamps
```

- `ratio = basePitch·2^(semis/12)·(rev ? −1 : +1)` exactly as now (:536-542); reverse grains have
  `ageInc = 1 + |ratio|` — they fall away from the head, always safe.
- **The catch-up guard (the one new law).** A pitched-UP forward grain (ratio r > 1) closes on the
  write head at (r−1) per sample. It must die (window = 0) before it arrives. Spawn law:

  `ageBirth ≥ len·max(0, r−1) + margin,  margin = blockSize + 4 (Hermite taps) + 1`

  With len ≤ 900 ms·fs and r ≤ 4 (+24 st) worst case ageBirth ≈ 2.7 s — well inside the 16.5 s
  ring. Clamp, don't reject: a too-close birth is pushed back in time (inaudible — it's a granular
  cloud). Pitched-down grains drift backward; guard the far edge: `age + len·(1−r) < N − margin`.
- **Scan / Position remap.** The osc engine's 0..1 region becomes the **window**: `Window` (back
  knob, synced) selects how many seconds/bars of the freshest ring is the playable region; `Scan`
  moves a virtual head through it exactly as `advanceHead()` does now (:452-503) — at Scan = +1 the
  head keeps pace with the writer (a live granulizer), at 0 it holds a fixed age (the living
  freeze), at −1 it dives into the past. Head position in age-space: `ageHead' = ageHead + (1 −
  scanRate)` per sample, folded into the window.
- **Freeze** stops `w` advancing? **NO** — freeze keeps `w` advancing but writes the blend
  (GrainEngine law). Ages stay valid, no discontinuity, and freeze becomes a 0..1 *knob* with a
  10 ms glide instead of a popping gate. At freeze = 1 the ring re-writes itself (bit-identical
  copy) — the held audio loops under the ages with period N. To keep the loop seamless the write
  at freeze = 1 is the identity — no fade needed. This is the single most elegant consequence of
  the write-blend law.
- The ring never clears on repeated `prepare` at the same fs (RollingCaptureBuffer.h:27-30 idiom).
- CPU of the ring: 2 mul-adds per sample of write — free.

### 3.3 ⚠️ THE FREEZE LAW TENSION (honest — decision belongs to Max)

Law 6 (nothing free-runs; sound dies with the note) collides with the whole point of a freeze that
sustains a pad after note-off. Options, with precedent:

| Option | Behavior | Law-6 status | Precedent |
|---|---|---|---|
| **A. Env-decayed freeze** | frozen cloud amplitude rides an FX-bus input follower; release 2-8 s (back `Freeze` knob position scales release); silence in ⇒ cloud fades out | fully compliant | the Phase G env-gated feedback law (AC-coupled, env-tracked) |
| **B. Latched freeze = explicit pill** | front `Freeze` pill latches infinite hold until the user un-clicks | free-runs **by explicit user gesture** | the Reverb device ALREADY ships exactly this: `SYN_RVB_FREEZE` front pill, infinite hold (ParameterIDs.hpp:367) |
| **C. Both** (recommended) | back `Freeze` knob = env-obedient regeneration amount (option A); front `Freeze` pill = the latched hold (option B) | knob compliant; pill = same consent Reverb already has | both of the above |

Recommendation C: the *knob* obeys the law (it is a regeneration amount, gated by input), the
*pill* inherits the Reverb Freeze pill's already-granted exemption. **Open Question #2.**

---

## 4. Types — 8 for the Type dropdown, each a named lineage with a measurable tell

All eight run on the ONE kernel (ring + pool + windows). A Type is a **spawner policy + pitch
policy + read policy** — night-and-day by construction, ~zero marginal code. Discriminators are
harness metrics per the fb283 perceptual law (magnitude-spectrum, centroid, flux, onset grid —
never sample-diff RMS).

| # | Type | Lineage | Recipe (delta from the base kernel) | 🔑 Discriminator (measured gate) |
|---|---|---|---|---|
| 1 | **Cloud** | Clouds/Fragments Texture | async Poisson spawner (the stock :158 countdown), Size mid-long, overlap 4-16, Shape→Hann/Bell | spectral flux LOW (<0.5× input flux at Density>50 %); overlap ≥4 measured by grain census |
| 2 | **Shimmer** | H3000 Crystal Echoes / Valhalla shimmer culture | Key forced ≥ Oct; per-grain +12/+7 draws (randKeyOffset weighting); **Feedback path re-enters the ring pitched** — every generation climbs | band-energy ABOVE the input's top partial GROWS per feedback pass: ≥ +6 dB/pass at Feedback 60 % (input band-limited probe) |
| 3 | **Swarm** | Roads' stochastic cloud / Emergence | Density pushed 2× (up to 440 g/s — Beads' "audio-rate density" edge), Size forced short (2-60 ms), Detune draws UNIFORM ±cents..±4 st (not key-snapped), per-grain pan full | sideband spread: autocorrelation peak at the probe period widens ≥3× vs Cloud; grain census ≥40 concurrent |
| 4 | **Freeze** | Clouds FREEZE / our "living freeze" | write-blend held at knob value; Scan≈0 default; grains spray from the held slice; Detune/Key make it chordal | output magnitude spectrum STATIONARY (frame-to-frame correlation >0.99) while the input probe CHANGES; input-vs-output spectral divergence grows |
| 5 | **Scatter** | Fragments Rhythmic / Portal synced rate | spawner is a **tempo clock** (Rate knob synced, 4 bars → 1/256), probability gate (Spray = skip/repeat chance), coin-flip reverse per hit, retrig quantized | onset autocorrelation peaks AT the clock lag (±2 ms) ≥12 dB above the floor; off-grid onsets <10 % |
| 6 | **Reverse** | Crystallizer reverse-splice | all grains reversed (dir = −1), Size long (100-900 ms), spawn ~1/overlap so splices tile; Feedback = Recycle (re-enters pitched if Key on) | cross-correlation of output vs TIME-FLIPPED input ≥3× its correlation vs the input; envelope attack-inversion (rise-time ratio out/in >4) |
| 7 | **Stretch** | Paulstretch / Clouds WSOLA STRETCH | head advances at 1/(1+3·Stretch-knob-position) through the window (the EXACT :460-464 Sample-parity law, headDiv_); long Bell grains, high overlap | event dilation: probe click train spacing out/in = headDiv (measure 1×→4×); pitch UNCHANGED (centroid ±5 %) |
| 8 | **Pulverize** | Clouds 8-bit µ-law "Fairlight" + GrainEngine Wander V3 | ring stored µ-law 8-bit + optional ÷2 SR (Clouds' kDownsamplingFactor law); Wander dice table verbatim (§2.3); per-grain tanh sat | noise floor rises to µ-law's −48 dB signal-correlated floor; SR images at fs/2ʲ; dropout census matches p-table ±20 % |

**Cut candidates if Max wants 6:** fold Reverse into Scatter (its coin-flip already reverses) and
Stretch into Freeze (Scan already slows) — but both survive the night-and-day gate as specced, and
the roster is the marketing sheet. **Open Question #3.**

### The Character dropdown — 6 media, orthogonal to Type (the Beads axis)

Beads proved the second axis is the **recording medium**, not the grain math: "cold digital / sunny
tape / scorched cassette". Ours (each = physics on the RING WRITE/READ path, never EQ):

| Character | Physics (all measured-able) |
|---|---|
| Clean | bypass — the reference |
| Tape | wow 0.4 Hz ±0.15 % + flutter 6 Hz ±0.05 % on read age; tanh at −8 dB rel. program (knee at −34 dBFS abs) on write |
| Cassette | µ-law 8-bit ring (Clouds law) + wow ×2 + head-bump LF +2 dB @ 80 Hz on read |
| Radio | ring band-passed 300-3.4 kHz 2nd-order on write + AM crackle gated by input env (law 6) |
| Worn | Wander dice at fixed i=0.35 (dropout/stutter/timing scatter) — GrainEngine table verbatim |
| Drift | per-grain start-age random-walked (Phase G Worn-walk law: a WALK, not a per-sample noise smoother) ±30 ms; ±15 cent per-grain detune walk |

Type-switch and Character-switch both **fade-swap-recover** (the Phase G deferred-char-fade +
re-seat law — a switch mid-note crossfades outputs over ~40 ms and re-seats state; never a hard
swap of the live cloud).

---

## 5. DSP core — math, param laws, stability, aliasing verdict

### 5.1 The spawner (per Type)

Async (Cloud/Swarm/Freeze/Reverse/Stretch/Pulverize): the stock countdown (:158-167),
`interval = fs/densHz`, jitter U(−0.5, +0.5)·interval. Clocked (Scatter): `interval =
hostSecondsPerDivision(div)·fs` from the Delay's resolver; jitter replaced by the probability gate.
Density map stays `pow(220, d)` g/s (:421) — Swarm doubles it post-map. Spawn refusal when the pool
or the **shared instance grain budget** (:97) is full = skip-and-wait, graceful (:508-509).

### 5.2 Per-grain pitch

`semis = Pitch + keyDraw(Key) + Detune·U(−1,1)·24` then `snapToKey` if Key>0 — verbatim :532-535.
`ratio = 2^(semis/12)`, clamp semis to ±24 (ratio ≤ 4). **Declick is structural**: a Pitch-knob
move affects only NEW grains; flying grains keep their birth ratio (the per-grain-capture pattern).
No zipper is possible on Pitch/Detune/Key by construction. Scan/Window/Freeze/Feedback DO need
glides (§5.6).

### 5.3 Windows and normalization

`windowAt(shape, skew, phase)` + the skew LUT — verbatim (:639-657, :626-637) **including the
fb234 never-built guard** (:429 — the 0<|skew|≤0.008 zero-LUT total-silence trap; copy the guard
line, do not re-derive it). Normalization `1/√overlap` with the 3 ms glide — verbatim (:427,
:203). This is what makes Density/Size loudness-neutral (law 5's "movement is the magic" without
level surprises) and it is the unity-through anchor of §8.

### 5.4 🔑 The feedback loop — LOOP GAIN LAW, stated

Path: cloud wet (L/R) → `× fbSm` → **DC blocker** (one-pole HP @ 20 Hz — pitch-down grains pile
LF; Phase G AC-coupled-loops law) → `tanh` soft-clip → **env-gate** `× gateSm` → summed into the
ring write (+ live input × (1−freeze…)). Gain stages inside the loop, every one accounted:

| Stage | Gain |
|---|---|
| cloud read (norm law) | ≈ 1.0 RMS (window-RMS-matched, overlap-normalized — measured property of §5.3) |
| Character sat (Tape etc.) | ≤ 1.0 (compressive) |
| Feedback knob | 0 .. **1.10** (the Delay's ~110 % precedent, ParameterIDs.hpp:378) |
| tanh clip | < 1 above the knee |
| env-gate | 0..1 |

Max small-signal loop gain = 1.10 ⇒ regenerative but the tanh knee (drive calibrated so the knee
sits at **−14 dBFS ≈ program +12 dB**; program = −26 dBFS, law 1) bounds every trajectory: growth
saturates instead of exploding. **The env-gate enforces law 6**: follower attack 5 ms, release
**squared-release mapped 0.2→4 s** (the Phase G squared-release law) from the Feedback knob's top
half; silence at the input ⇒ gate → 0 ⇒ the spiral dies with the note. Shimmer's climb, Reverse's
recycle and Freeze's regeneration all live inside this ONE bounded loop.

### 5.5 Aliasing / oversampling verdict

Pitch-up grains resample by up to 4×; 4-tap Hermite's images at r=4 sit ≈ −40 dB under a windowed,
jittered, overlapped cloud — and 50 years of this effect (H910 → Crystallizer → Clouds) ship the
artifact as the *product* (§1). Verdict: **1×/no oversampling anywhere in this device, at every
Quality tier.** The one guard: Swarm's audio-rate density edge AM-modulates (sidebands, not
aliases) — that is the Beads-documented sound, keep it. µ-law (Cassette/Pulverize) is definitionally
un-oversampled (distortion bible Family-C law: the artifacts ARE the product; baseline AA = none).
This is also the CPU story: the whole device is arithmetic + table reads, no FFT, no upsampler.

### 5.6 Param smoothing table (law 7)

| Param | Glide | Mechanism |
|---|---|---|
| Density, Size | none needed | per-grain capture + norm glide (3 ms) absorbs it (:198-206) |
| Pitch, Detune, Key, Width, Dir, Shape*, Skew* | none needed | per-grain capture (*Shape/Skew additionally ride normAlpha per :204-206) |
| Scan, Window | 15 ms one-pole | head-rate/age-span glide (comb-click law — an age jump is a delay-length jump) |
| Freeze | 10 ms one-pole | write-blend coefficient |
| Feedback | 15 ms one-pole | fbSm |
| Mix | equal-power, 15 ms | processor-side, 100 % = fully wet (dry residual < −60 dB, house verify) |
| Type/Character | 40 ms fade-swap-recover | Phase G deferred-fade + re-seat |
| Rate (synced) | quantize on next clock edge | never mid-interval (Scatter clock stays phase-coherent) |

### 5.7 Denormals / NaN

`ScopedNoDenormals` at the block top (house standard); flush the follower + glide one-poles; keep
GrainEngine's `isfinite` wet-sum guard (:198-200) — a single NaN grain must not poison the ring
(it would recirculate FOREVER through feedback: NaN × freeze-blend never clears).

---

## 6. Chassis map — the 11 params on the locked fb275 chassis

**Device:** `Granular` · engine `tw::GranularFxEngine` · params `SYN_GRN_*` · POWER default **OFF**
(distortion precedent — dry init, pluginval-safe). Route bools `SYN_GRN_SRC_A/B/C/D/SUB/NOISE`
default OFF; main-send behavior identical to the other three devices.

### Front card (Max's mandate: the osc granular layout, rack-sized, header/footer added)

The card front IS the sample-view shell + gran-knob-wrap clone (§2.1 anchors), re-skinned: the
waveform is the **live capture ring** (§7), the region handles become the **Window** span, the
`.samp-ph` follower is the scan head, `.samp-drop`/`.samp-exp`/fades deleted. Hero knobs on the
card (the fb275 "3 + Mix"):

| Front knob | Param | Range / taper | Why it's front |
|---|---|---|---|
| **Grains** | `SYN_GRN_DENSITY` | 0..1 → 1..220 g/s log (Swarm ×2) | THE granular knob — sparse blips → solid cloud |
| **Size** | `SYN_GRN_SIZE` | 0..1 → 2..900 ms log | the texture axis |
| **Pitch** | `SYN_GRN_PITCH` | ±24 st, stepped-feel detents at ±7/±12 | the shimmer axis |
| **Mix** | `SYN_GRN_MIX` | 0..1 equal-power, **100 % = fully wet** | law 4 |

Pills: `Freeze` (latched hold — §3.3 option C) · `Sync` (Rate/Window follow tempo) · power.

### Back panel — 2 dropdowns + 8 knobs (4×2)

**Dropdown 1 — Type (8):** Cloud · Shimmer · Swarm · Freeze · Scatter · Reverse · Stretch ·
Pulverize.
**Dropdown 2 — Character (6):** Clean · Tape · Cassette · Radio · Worn · Drift.

| Slot | Name | Param | Range / taper | Glide | Does |
|---|---|---|---|---|---|
| 1 | **Scan** | `SYN_GRN_SCAN` | −1..+1, center detent 0 | 15 ms | head rate through the window: −1 dive into the past · 0 hold · +1 ride the live head |
| 2 | **Window** | `SYN_GRN_WINDOW` | synced **4 bars → 1/256** (law 3) / free 50 ms..16 s log | 15 ms (age-span) | how much of the freshest past is the playable region |
| 3 | **Spray** | `SYN_GRN_SPRAY` | 0..1 → ±half window (Scatter: skip/repeat probability) | per-grain | birth-position chaos |
| 4 | **Detune** | `SYN_GRN_DETUNE` | 0..1 → ±24 st scatter (key-snapped when Key on) | per-grain | per-grain pitch scatter — the shimmer/swarm fuel |
| 5 | **Shape** | `SYN_GRN_SHAPE` | 0..1 Flat→Hann→Bell (+Skew via right-click? NO — see OQ #5) | normAlpha | grain window = attack character of every grain |
| 6 | **Width** | `SYN_GRN_WIDTH` | 0..1 per-grain equal-power pan | per-grain | mono beam → full scatter field |
| 7 | **Feedback** | `SYN_GRN_FEEDBACK` | 0..1.10, t^1.5 taper (drama at the top) | 15 ms | wet re-entry into the ring (§5.4); Shimmer's climb, Reverse's Recycle |
| 8 | **Freeze** | `SYN_GRN_FREEZE` | 0..1 write-blend | 10 ms | continuous regeneration → full hold (env-obedient; the pill latches) |

`Key` (Off/Oct/5th/Chord/Maj/Min/Penta) rides the **Detune knob's right-click** exactly as the osc
page's stepped Key knob… **NO — dropdowns-not-click-rotate is absolute.** Key therefore lives as
the 7 entries appended to nothing: it is **Character slot? No.** Resolution: `Key` is a stepped
back knob is what the OSC page ships (`SYN_OSC_A_GRAIN_KEY` IS a knob, §2.1) — but the fb275 grid
is full. **Decision proposed: Type carries the key policy** (Shimmer forces Oct/5th; other types
read a global `SYN_GRN_KEY` choice exposed on the FRONT header as a small dropdown pill, the
`engine-select` idiom — same pattern as the delay's front Sync-div selector). **Open Question #4 —
Max picks:** (a) front mini-dropdown Key (recommended — it's the differentiator, show it), (b)
sacrifice Width's slot to a stepped Key knob (osc-page parity), (c) Shimmer-only hardwired keys.

Every knob: 0→100 continuous audible evolution, no plateaus (law 5) — the §9 harness sweeps all 8.

---

## 7. Visualizers — survey, then our card

### How the greats draw granular (mechanisms, precisely)

| Product | Mechanism |
|---|---|
| **Arturia Efx Fragments** | buffer **waveform strip**; a **white record-head** cursor sweeps as the ring writes; **yellow playback markers** show grain read points; a separate "Grain Release" dynamic visualizer animates grain envelopes; Advanced view adds a decorative 3D field (SOS review) |
| **Output Portal** | full-window **particle field** — each grain a glowing particle whose position/velocity encodes pan/pitch; the central circular **XY macro dial** doubles as the hero visual (MusicRadar) |
| **Ableton Grain Delay** | no grain viz at all — an XY param pad only. The gap that made everyone else's viz a selling point |
| **Mutable Clouds/Beads** | hardware: LED per grain-burst + blend LEDs — the minimal "grain census" display |
| **Emergence** | animated grain-activity display (600 grains); KVR users cite watching it as part of the appeal |
| **Serum 2** | n/a — no granular FX; its FX culture = the response-curve-over-signal overlay, which doesn't fit grains |

### Our card — 2 concepts (canvas, CPU-cheap, idle=dim / playing=bright)

**A. THE RING (recommended — it IS Max's mandated layout).** The sample-view waveform becomes the
live capture ring, drawn oldest→newest left→right (Fragments' strip, our shell): a **write-head
cursor** sweeps right edge; the waveform scrolls when Scan rides the head, holds when frozen
(freeze visibly dims the writer trace to 20 % — obvious delta). Over it, **grain sparks** from
`cloudSnapshot()` (GrainViz already carries pos01/age01/pan — GranularEngine.h:33, :326-340; the
follower push channel already exists for the osc page): each spark born at its read position,
y-offset = pan, radius = window value at its age (they visibly swell and die with the actual
window Shape), color temperature = pitch (down=amber, unison=white, up=cyan — Key quantize makes
visible stripes at +7/+12). Idle: flat dim line, no sparks. Every param visibly moves it: Density=
spark count, Size=spark lifetime, Spray=horizontal scatter, Width=vertical scatter, Window=region
brackets (the samp-h handles, recycled), Scan=head motion, Freeze=writer dim + held waveform,
Feedback=sparks leave fading trails that re-enter. Cost: one waveform path redraw per frame reusing
`__terrainSampleRedraw`'s canvas + ≤64 arcs — no shadowBlur, no filters (the fb342 per-frame law).

**B. GRAIN FOUNTAIN (the Portal answer).** Particles erupt from a base line at grain birth,
vertical velocity = pitch ratio (up-shifted grains RISE, reversed grains fall backward), gravity
returns them at window-end. Dramatic, but it hides the ring/freeze state that concept A shows, and
it is new drawing code where A recycles the shell Max already mandated. Ship A; keep B as the
possible "swapped" alt-view later.

---

## 8. Interplay — the device in the chain

- **Unity-through:** at defaults (Mix 50 %, Feedback 0, Clean, Cloud, Density mid) wet RMS ≈ input
  RMS **by the §5.3 norm law** — the certified property to gate: full-mix level within ±1.5 dB of
  bypass on the −26 dBFS program probe.
- **Latency: ZERO reported, zero actual.** All reads are strictly behind the write head — the
  device is causal, unlike the distortion's oversampling latency trap (§4.4 there). Nothing to
  compensate; the fb305 send maths stay exact.
- **Spectrum downstream:** Cloud/Freeze/Stretch are near-energy-preserving but *decorrelate* — a
  reverb after them blooms wider; the classic order is **Distortion → Granular → Delay → Reverb**
  (grain the saturated tone, echo the grains, wash the echoes). Granular BEFORE distortion turns
  every grain edge into a click-exciter — legal, loud, name it in the manual.
- **Stacking trap:** Granular Feedback + Delay Feedback = two coupled loops. Each is individually
  bounded (tanh-kneed, env-gated); their series gain multiplies but both gates die with the input,
  so the product cannot free-run (law 6 holds transitively). Still: the presets never ship both
  >60 %.
- **Mono-sum:** per-grain equal-power pan (cos/sin, :555-558) sums to constant power — mono
  collapse loses Width but no comb (grains are mutually incoherent). Safe.
- **The fb305/fb338 wiring** (§2.5): four sum-lines + relay 4-point chain (`.withOptionsFrom`,
  attachment, JS read — the CLAUDE.md §4 checklist) or the device silently no-ops.

---

## 9. Presets — 13 factory sketches (names Title-case, pragmatic)

| # | Name | Type/Char | Sketch (front · back) |
|---|---|---|---|
| 1 | First Cloud | Cloud/Clean | Grains 55 · Size 40 · Pitch 0 · Mix 45 · Scan +0.3, Window 1 bar, Spray 25, Shape 60, Width 60 |
| 2 | Halo Choir | Shimmer/Clean | Grains 60 · Size 65 · Pitch +12 · Mix 55 · Detune 35 (Key Oct+5th), Feedback 65, Width 80 |
| 3 | Bee Math | Swarm/Clean | Grains 90 · Size 8 · Pitch 0 · Mix 60 · Spray 70, Detune 20, Width 100, Shape 15 |
| 4 | Amber Hold | Freeze/Tape | Grains 50 · Size 70 · Mix 70 · Freeze 85, Scan 0, Shape 85, Detune 12 (Key Maj) |
| 5 | Sixteenth Rain | Scatter/Clean | Sync on, Rate 1/16 · Size 20 · Mix 50 · Spray 30 (skip), Width 70, Feedback 25 |
| 6 | Tape Ghost | Reverse/Tape | Grains 35 · Size 80 · Pitch 0 · Mix 55 · Feedback 55, Window 2 s, Width 40 |
| 7 | Glacier Four | Stretch/Clean | Stretch head ÷4 · Size 85 · Mix 65 · Shape 90, Spray 15, Freeze 30 |
| 8 | Fairlight Dust | Pulverize/Cassette | Grains 70 · Size 25 · Mix 60 · Wander-heavy, Detune 10, Feedback 30 |
| 9 | Fifth Fountain | Shimmer/Drift | Pitch +7 · Key 5th · Feedback 75 · Size 55 · Mix 50 — the drifting +7 spiral |
| 10 | Vinyl Memory | Cloud/Worn | Grains 45 · Size 50 · Mix 40 · Spray 40, dropout crackle rides the input |
| 11 | Radio Séance | Freeze/Radio | Freeze 100 (pill) · Size 60 · Mix 80 · band-limited hold + env crackle |
| 12 | Downstairs | Cloud/Clean | Pitch −12 · Detune 8 · Grains 65 · Mix 45 — the octave-under thickener |
| 13 | Last Breath | Stretch/Tape | Stretch max · Freeze 60 · Mix 100 — note-off leaves a 4 s dying exhale (env-decayed — the law-6 demo) |

Preset level discipline: all thirteen within ±2 dB of unity-through at the −26 dBFS program probe
(the Phase G preset-level-spread lesson: Sludge/Gargle spread was the flagged defect class).

---

## 10. CPU — budget and tiers

Measured anchors from the oscillator engine: the per-sample full-pool scan was 72 % of engine cost
and is already replaced by the compact active list (:169-172); `renderBlockAdd` keeps grain state
in registers (:228-234); windows are shared statics (:570). The FX instance is ONE engine (vs up
to 96 voice-engines on the osc side) — the osc side is the proof of headroom.

- Estimate: overlap 8 (typical Cloud) ≈ 8 live grains × (Hermite 4-tap ×2 ch + window lerp + 2
  mul-add) ≈ **well under 1 % of a core**; worst case (Swarm, 64-grain pool saturated) ≈ 3-4 %.
  Ring write + feedback path ≈ noise.
- **Join the instance-wide grain budget** (`setGrainBudget`, :97) so FX + osc granulars degrade
  together gracefully (skip-and-wait, never glitch).
- **No Quality tiers, no oversampling** (§5.5). The Quality dropdown slot this device doesn't
  need is exactly why Character got the second dropdown.
- Control head sleeps when POWER off (the fb342 awake-head-sleep law); the ring does NOT write
  while powered off (true bypass = zero work), and clears on power-on to avoid granulating stale
  program (a deliberate divergence from the prepare-preserve rule — power-off is user intent).
- Viz: ≤64 arcs + one path per rAF, no shadowBlur/filters (fb342), push lanes only while visible ×
  fresh (session law ③).

---

## 11. Pitfalls — the collected traps

1. **Write-under-read** — a pitched-up grain overruns the write head = reads the seam = wideband
   click. The §3.2 catch-up guard formula is the fix; assert it in the harness (probe: +24 st,
   900 ms grains, sweep Density — zero clicks above the honest per-char click floor).
2. **Freeze pop** — gating the WRITE (not blending) steps the ring by a full block. Use the
   write-blend with 10 ms glide (§3.2); the pill drives the same coefficient.
3. **Feedback NaN/DC latch** — NaN or DC entering the ring recirculates forever (the write-blend
   preserves it at freeze>0). DC blocker + isfinite guard INSIDE the loop (§5.4, §5.7) — this is
   the granular twin of Phase G's DC-LATCH SILENCE CLASS.
4. **Norm click** — any Density/Size jump without the 3 ms norm glide clicks the whole cloud
   (measured, :198-203). Recycle the glide, don't re-derive.
5. **The fb234 skew-LUT never-built trap** — copy the guard at :429 verbatim if Skew ships;
   0<|skew|≤0.008 built a zero LUT = TOTAL SILENCE once.
6. **Choice params read the INDEX** — `getRawParameterValue` returns the choice index as float
   (CLAUDE.md §4; the fb50 noise bug). Type/Character/Key/Rate-div all read `(int)*raw`.
7. **Zipper on Scan/Window** — an age jump IS a delay-length jump (comb-click law). Glide the
   age-span, never snap; fold the head with fmod in ONE step (the :496-499 no-spiral idiom).
8. **Stale ring on rate change** — reallocate + clear only when fs actually changes
   (RollingCaptureBuffer idiom :27-30); DAW transport prepare-spam must not wipe a frozen pad.
9. **Type-switch cloud murder** — swapping spawner policy under 64 flying grains: grains keep
   their birth policy (per-grain capture), only the SPAWNER switches, output fade-swap 40 ms
   (Phase G law). Never resetPool() on a live switch.
10. **Send-bus silent no-op** — the 4-point relay chain per param (CLAUDE.md §4) AND the three
    fb305 exclusion lines + the new fourth (§2.5). Both fail silently.
11. **Mix law verify** — 100 % wet must leave dry residual < −60 dB including the main-send path.
12. **Budget leak** — grains released to the shared budget on resetPool (:612-616 pattern);
    the FX engine must mirror it or the osc granulars starve after a few FX power cycles.
13. **auval/pluginval** — POWER default OFF ⇒ dry init ⇒ null-test-safe out of the box
    (distortion precedent, ParameterIDs comment at :406 block).

---

## 12. Hard-rule compliance checklist (laws 1-10, walked)

| # | Law | Where honored |
|---|---|---|
| 1 | Bus reality −26 dBFS | feedback knee at −14 dBFS = program +12 (§5.4); env-gate opens −56 dBFS = program −30; Character sat knees stated absolute (§4); no literature gain copied anywhere |
| 2 | Chassis 2 dropdowns + 4×2 + pragmatic names | §6 — Type/Character + Scan·Window·Spray·Detune·Shape·Width·Feedback·Freeze; every name says what it does, Title-case |
| 3 | Time params 4 bars → 1/256 | Window synced range + Scatter Rate divisions (§6 slot 2, §5.1) |
| 4 | Mix 100 % = fully wet; switches never cut | §5.6 Mix row; Type/Character fade-swap-recover |
| 5 | Params evolve 0→100, Types night-and-day | per-knob tapers §6; per-Type measured discriminators §4; harness sweeps §9/§13 |
| 6 | Nothing free-runs | env-gated feedback (§5.4), env-decayed Freeze knob (§3.3 A), Radio crackle input-gated (§4); the ONE exemption (Freeze pill) inherits Reverb's shipped precedent and is flagged to Max (§3.3) |
| 7 | No clicks | §5.6 table; per-grain capture declick; write-blend freeze; catch-up guard |
| 8 | CPU | §10 — <1 % typical, no oversampling, shared budget, sleep on power-off |
| 9 | Audible ⇒ visible + dramatic | §7A — every one of the 11 params has a named visual consequence; idle=dim line, playing=spark field |
| 10 | Recycle first | §14 — the engine is ~70 % verbatim reuse, the UI shell is 100 % reuse |

---

## 13. Verify — the perceptual harness gates (fb283 law)

Per-family cert pattern (`dst_cert_*` grammar, clang++ -O2 -I shim -I Source):

- **Type discriminators**: the §4 table's eight gates, each a number with a threshold.
- **Unity-through**: ±1.5 dB at defaults on the −26 dBFS program probe.
- **Law-6 gate**: feed 2 s program then silence; assert output < −60 dBFS within 8 s at ANY knob
  state except the latched pill.
- **Click floor**: AM-probe sweeps of Scan/Window/Freeze/Feedback + Type/Character switches
  against the honest per-char click floor (Phase G probe-craft: PK_AM for static duck, silence
  metric for the freeze axes).
- **Loop stability**: Feedback 110 % + Shimmer + Cassette worst case, 60 s program: peak bounded
  < −3 dBFS, no growth after gate release.
- **Mix wet law**: dry residual < −60 dB at 100 %.
- **Knob evolution**: every back knob swept 0→100 in 10 steps — monotone audible delta per the
  metric of its lane (no plateau > 15 % of travel).

---

## 14. Recycle inventory — exact reuse, verified by read

| Reused thing | Source | Into |
|---|---|---|
| grain pool + active/free lists + spawner + retire | GranularEngine.h:158-196, 353-373, 506-564, 609-621 | GranularFxEngine verbatim |
| windows LUT + RMS match + windowAt + skew LUT + fb234 guard | :566-657, 626-637, 429 | verbatim |
| norm 1/√overlap + 3 ms glide | :418-430, 198-207 | verbatim |
| snapToKey / keyTable / randKeyOffset | :683-724 | verbatim (Key feature) |
| Hermite read | :662-673 | re-based onto ring (mask wrap replaces clamp) |
| cloudSnapshot + GrainViz push | :33, :326-340 + the osc follower channel | card viz §7A |
| setGrainBudget shared budget | :97, 508-509, 612-616 | join it |
| write-blend freeze + Wander V3 dice + NaN guard | GrainEngine.h:112-115, 41-52, 195-200 | Freeze knob · Worn/Pulverize · loop guard |
| prepare-preserve idiom | RollingCaptureBuffer.h:27-30 | ring prepare |
| 16.5 s alloc + mask ring + 15 ms smCoef + sync resolver + fb 110 % | DelayEngine.h:38-64 + processor resolver | ring size · glides · Rate/Window sync |
| fade-swap-recover + squared release + AC-coupled loop laws | Phase G ledger (terrain-instrument-phase-g-certification-fb345) | switches · env-gate · feedback |
| sample-view shell + gran-knob-wrap + gk-arrow + samp-h handles | index.html:4273-4407, 5899-5966, 13790-13795 | the card front, re-skinned |
| engine-select dropdown idiom + .pmenu presets | house canon | Type/Character/Key menus + device presets |
| fb305 exclusion sums | PluginProcessor.cpp:7159, 7326, 7357-7358 | + grnSend terms, + the 4th block |

---

## 15. Open questions for Max

1. **Chassis vs mandate**: the card front = the osc granular layout (your mandate) — I mapped
   heroes Grains/Size/Pitch/Mix onto it; do you want the FULL two-page 12-knob rows on the card
   front (mirroring back params), or the shell + 4 heroes with the rest back-panel-only?
2. **The Freeze law tension** (§3.3): env-decayed knob + latched pill (recommended C), or strict
   env-decay only?
3. **Roster size**: 8 Types as specced, or cut to 6 (fold Reverse→Scatter, Stretch→Freeze)?
4. **Where Key lives** (§6): front mini-dropdown (recommended), a back knob displacing Width, or
   Shimmer-hardwired?
5. **Skew**: ship it as Shape's right-click depth (osc parity) or drop it from v1?
6. **Buffer**: 16.5 s (4-bar parity with Delay, +6.3 MB/instance) or 8 s (half the RAM, caps
   Window at 2 bars @ 60 BPM)?
7. **Name**: `Granular` vs `Grains` on the rack slot.
8. **Ableton Grain Delay numbers** in §1 are manual-sourced but unverified this run (search budget)
   — OK to ship the history section as-is, or want a verify pass?

---

## 16. Sources

- Mutable Instruments Clouds manual — https://pichenettes.github.io/mutable-instruments-documentation/modules/clouds/manual/
- Mutable Instruments Beads manual — https://pichenettes.github.io/mutable-instruments-documentation/modules/beads/manual/
- Clouds Parasites firmware docs — https://mqtthiqs.github.io/parasites/clouds.html
- Clouds source, granular_processor.h (modes, µ-law, buffer trades) — https://github.com/pichenettes/eurorack/blob/master/clouds/dsp/granular_processor.h
- Arturia Efx Fragments overview — https://www.arturia.com/products/software-effects/efx-fragments/overview
- Sound On Sound: Arturia Efx Fragments review (UI layout, buffer range, capture modes) — https://www.soundonsound.com/reviews/arturia-efx-fragments
- Soundtoys Crystallizer (H3000 Crystal Echoes lineage) — https://www.soundtoys.com/product/crystallizer/
- MusicRadar: Output Portal review (params, XY macro, FX list) — https://www.musicradar.com/reviews/output-portal
- Argotlunar (GPLv2 realtime granulator) — https://github.com/mourednik/argotlunar
- KVR: Emergence by Daniel Gergely (600 grains / 4 streams) — https://www.kvraudio.com/product/emergence-by-daniel-gergely
- Ableton Live manual, Grain Delay (unverified this run) — https://www.ableton.com/en/live-manual/12/live-audio-effect-reference/
- Julius O. Smith, Spectral Audio Signal Processing, Time-Scale Modification — https://ccrma.stanford.edu/~jos/sasp/Time_Scale_Modification.html
- Curtis Roads, *Microsound*, MIT Press 2001 (book — the granular canon).
- In-repo primary sources: GranularEngine.h, GrainEngine.h, RollingCaptureBuffer.h, DelayEngine.h,
  ParameterIDs.hpp, PluginProcessor.cpp/h, ui/public/index.html, DISTORTION-BUILD-BIBLE.md.
