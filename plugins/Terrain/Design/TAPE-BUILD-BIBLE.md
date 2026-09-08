# Terrain Instrument — Tape Build Bible

**The device:** `Tape` — the fourth flagship FX device. ONE device, the **MACHINE**: echo heads on a
free-running loop, transport physics (motor, wow, flutter, inertia), head EQ, a light record-path
saturation stage, env-gated hiss, splice, and a self-oscillating feedback loop with a limiter inside it.
**Serum 2 has NO tape echo** (its FX menu: Bode, Chorus, Compressor, Convolve, Delay, Distortion,
Equalizer, Filter, Flanger, Hyper/Dimension, Phaser, Reverb, Splitters, Utility — verified against Max's
screenshot of the Serum 2 FX list). After the user-IR convolution reverb, **this is the second headline
differentiator**: a full transport-physics echo machine inside a $99 synth.

Reading order for the builder: §0 (the boundary — read FIRST, this is Max's tangling fear),
§3 (DSP core), §4 (chassis), §2 (per-Type specs), §10 (pitfalls), §11 (hard-rule walk). Everything is
calibrated to the measured −26 dBFS FX bus (house law 1) — no literature number was copied verbatim.

> 🔍 **AUDITED 2026-08-14 — READ §14.1 BEFORE QUOTING ANY NUMBER FROM THIS FILE.**
> This bible was written by a researcher and shipped unaudited. An adversarial pass has now re-read every
> in-tree file:line claim and re-fetched every reachable source. **Ten factual corrections were applied**
> — the headliners: the wow/flutter split is **4 Hz not 6 Hz**; the head-bump speed law was **inverted**;
> the self-osc **park level was off by 4.7 dB** (an invalid asymptotic approximation); §3.5 **misquoted**
> the Distortion bible and the "zero oversampling" verdict is **withdrawn** (S1's floor is `ADAA-1 + 2×`);
> Drive D_max is **+30 dB not +36**; the buffer is **8.4 MB not 5.3 MB**. Three latent divide-by-zero /
> sign faults in the `v→0` and `v<0` transport paths were found and guarded. Six recycle-inventory line
> numbers were wrong. **Everything the audit could not verify is now flagged inline as UNVERIFIED** —
> if a claim carries no ✅ and no ⚠️, it is in-tree and was re-read. Full log + the unverifiable list: §14.1.

---

## 0. Scope decision + THE BOUNDARY (three tapes already live in this tree)

### 0.1 The recon — what exists today (all verified by reading the code, file:line)

| System | Where | What it is | Fate |
|---|---|---|---|
| **DLY-panel tape section** (Studio/Cassette/Wire machines, TAPE/SPACE/**JUNE** pages) | `Source/TapeMachines.h:331/491/753` (Studio/Cassette/Wire class heads — **verified**), `Source/TapeProcessor.h:18` (**verified**), UI: greyout CSS `Source/ui/public/index.html:1017-1051` (`#tape-fx-container`), page nav + container markup `:5411-5421`, page names `:21595` (`FX_PAGE_NAMES = ['TAPE','SPACE','JUNE']` — the 3rd page is the **JUNE chorus** page `fxPageChorus`, **not** EQ; the stale `// Tape / Space / EQ` comment at `:21590` is wrong), randomizer hook `:19876` | Channel-strip tape **color** (sat + wow + hiss per machine), NO echo heads, NO transport loop | **Stays.** It is the Terrain-patcher tape insert. The Tape device recycles its building blocks (§13) but does not replace it |
| **Tape LOOP** | `Source/TapeLoopProcessor.h:30` (`class TapeLoopProcessor`; `MAX_BUFFER_SECONDS = 60.0` at `:33`, BPM-synced looper, varispeed presets `−3…+3×` at `:448`, Hermite reads, per-pass degrade LP `:552-558`) | A performance looper/recorder | **Stays.** Different job (recording), not an echo |
| **Distortion → ANALOG → Tape mode** (+ `Worn` character) | `Source/DistortionEngine.h:1653-1700` (`tapeF()`, JA cook; `Worn` = `chr_ == 6` at `:1685`), spec in `Design/DISTORTION-BUILD-BIBLE.md` §9.1 | Full Jiles-Atherton **hysteresis saturation** — the deep magnetic nonlinearity, 4× oversampled (DST bible `:443`) | **Stays.** It is the SATURATION authority |
| **DLY device → Tape character** | `Source/DelayEngine.h:274-280` (`tape()`: tanh drive `1.4 + 1.2·(character&1)` = 1.4/2.6, asym `tanh(drive·(x + 0.06x²))`, ~7 kHz per-pass LP, ×0.86 trim; wow stack `:154-163`) | One voicing of a generic delay | **Stays** as-is. It is a delay *flavor*, not a machine |

### 0.2 🔑 THE BOUNDARY LAW (so the devices never tangle)

```
DISTORTION.Tape   = the MAGNETISATION.   Hysteresis loop physics. What the tape IS.
TAPE (this device)= the MACHINE.         Heads, motor, loop, splice, feedback. What the tape DOES.
DLY.Tape          = a delay FLAVOR.      One tanh+LP color inside a clean delay chassis.
TapeMachines.h    = the CHANNEL COLOR.   Static sat/wow/hiss insert, no echo, no loop.
```

Concretely, inside the Tape device the saturation stage is a **cheap memoryless soft stage**
(§3.5) — deliberately NOT the Jiles-Atherton model. If the user wants deep hysteresis smear they chain
`Distortion (ANALOG/Tape)` → `Tape`, and the two devices compose instead of competing. The Tape
device's identity lives in the **transport**: every echo it makes rides ONE motor clock, and everything
audible (pitch, timing, level, bandwidth) derives from that clock. No other device in Terrain — and
nothing in Serum 2 — has a transport.

### 0.3 What is IN
- 7 Types (§2): `Space` · `Plex` · `Drum` · `Deck` · `Cassette` · `Worn` · `Reels` (the cut candidate).
- Multi-head echo patterns (the RE-201 12-mode selector, reshaped to our Character dropdown).
- Motor physics: Time changes GLIDE THE MOTOR → repeats pitch-bend (the tape-echo signature, §3.1).
- Wow + flutter + scrape with real spectra (§3.2), env-gated hiss (§3.6), splice thump (§3.7),
  dropout/crinkle event machines (§3.8), self-osc feedback with an in-loop limiter (§3.3).
- Tape-stop (`Stop` pill) and sound-on-sound freeze (`SOS` via Character on `Reels` / long-press law → open question Q6).

### 0.4 What is OUT, and where its sound went
- **Spring reverb** (the RE-201's other half): OUT — the Reverb device owns springs (`SpringReverb.h`,
  SIMD-certified fb342). The classic RE-201 "mode 5-11 echo+reverb" sound = `Tape` → `Reverb(Spring)` in
  the chain. §12 Q1 asks Max whether a tiny fixed spring send earns an exception.
- **Deep hysteresis**: OUT → Distortion ANALOG/Tape (§0.2).
- **Recording/looping**: OUT → TapeLoopProcessor.
- **Reverse playback**: folded into `Reels` characters, not a global toggle (keeps the transport one-directional
  for every other Type — reverse read on a shared loop with feedback is a click factory).

---

## 1. History and circuits — the lineage that defined the echo

### 1.1 Maestro Echoplex EP-2 / EP-3 (1959→1970) — the moving head
The Echoplex's core innovation was a **moving record head**: delay time changes by sliding the head
along the tape path, **not** by changing tape speed. ✅ **AUDIT-VERIFIED (2026-08-14):** Wikipedia/Echoplex
— *"a moving record head, which allowed for variable delay time without changing the tape speed"*;
the **EP-3** is the transistorised model, production *"beginning in 1970"*, and it is the EP-3 that
*"offered a sound-on-sound mode"*; the tape **cartridge** is confirmed (Battle's improvements: *"the
adjustable tape head and a cartridge containing the tape"*). ⚠️ *The 1959 EP-1 date is UNVERIFIED.*
Consequence — the defining
Plex behavior our §2.2 models: moving the Time control produces a one-shot Doppler slur *while the head
moves*, but material already circulating does NOT re-pitch. Tube EP-2s were loved for the "warm, round,
thick echo" of the tube preamp; the 1970 solid-state **EP-3** added sound-on-sound, and its preamp
became famous on its own (guitarists ran it with the echo off — EVH's "brown" front end). The tape
lived in a protective cartridge; worn carts = darker, duller repeats.

### 1.2 Roland RE-201 Space Echo (1974) — the multi-head machine
One record head + **three playback heads** at increasing distances along a **free-running tape loop**
(not a cassette — loose tape in a bin, so splice wear is gentle and the loop runs for years).
✅ **AUDIT-VERIFIED (2026-08-14):** Wikipedia/Roland RE-201 — *"The original Space Echo units contain a
single recording head and three playback heads"*, released **1974** (RE-101 + RE-201), *"free-running
tape transport system"*. A rotary **12-position Mode selector** picks head combinations: **4 echo-only
modes, 7 echo+reverb modes, 1 reverb-only** — ✅ **AUDIT-VERIFIED** against AudioThing Outer Space
(*"12 different combinations … 4 echo-only modes, 7 echo+reverb configurations, and 1 reverb-only"*);
Arturia's TAPE-201 independently states "3 delay tape heads + reverb tank in 12 combinations".
⚠️ *Loop length and per-head millisecond figures remain UNVERIFIED — see §12 Q7; ours are designed, not
quoted.* **Repeat Rate** is the motor
speed — turning it re-pitches everything currently on the loop (all heads share one clock — the
coherent-wobble signature §3.1). **Intensity** is loop feedback; at max it runs away into the
celebrated bounded self-oscillation ("the cool runaway feedback when the Intensity knob is maxed" —
Boss RE-202 page). Boss's RE-202 revival adds a 4th head, new/aged tape conditions, a Wow & Flutter
control, a Saturation control, and Warp/Twist performance effects — a checklist of what modern players
actually want from the machine, and close to our back-8.

### 1.3 Binson Echorec (late 1950s) — the drum, not the tape
A rotating **magnetic drum** with fixed record and playback heads (✅ **AUDIT-VERIFIED:** Wikipedia/Binson
Echorec — *"uses an analog magnetic drum recorder instead of a tape loop"*. ⚠️ *The **4** playback-head
count, the "Swell" mode behaviour and the production years are UNVERIFIED — Wikipedia's article is a stub
and no service manual was reachable this session. Our 4-head 1:2:3:4 geometry in §2.3 is **designed**, not
quoted; do not put it in marketing copy without a manual.*) No tape transport → almost **no wow**,
no splice, no dropouts; instead crisp geometric multi-head patterns ("Swell" mode regenerates all heads
into a proto-reverb wash). The drum is why Echorec repeats stay solid and chimey where tape smears —
our `Drum` Type's measurable identity (pitch-deviation σ < 0.01% vs Space's 0.1%+, §2.3).

### 1.4 The studio decks — Studer/ATR slap, ADT, and Strymon's Deco reduction
Studio echo wasn't a box: it was a **second tape machine** (7.5/15 ips) looped record→play — the
50s slapback (~80-180 ms, one repeat, clean wide bandwidth) and Abbey Road's ADT (double-tracking by
~10-40 ms wobbling offset). Strymon **Deco** reduces this to a two-deck architecture — a Reference Deck
plus a **Lag Deck spanning −0.3 ms → 500 ms** with `Wobble`, and a Wide Stereo mode that routes the
two decks to opposite sides. ✅ **AUDIT-VERIFIED (2026-08-14):** strymon.net/products/deco FAQ —
*"The range of time for Deco's delayed Lag Deck is from -.3ms to 500ms"*; Wide Stereo — *"input is then
sent to the left output channel through the Reference Deck, and to the right output channel via the
delayed Lag Deck"*; the five knobs are Saturation · Volume · Blend · Lag Time · Wobble.
This is our `Deck` Type: the only
Type whose Time floor drops to flange-adjacent territory.

### 1.5 Cassette (1⅞ ips) and the lo-fi canon
Compact cassette moved tape culture to a format with 4× less tape speed: bandwidth ceiling ~8-12 kHz,
weighted wow/flutter around **0.08% (audible)** vs **0.02% (pro, inaudible)** (Wikipedia, wow &
flutter measurement), motor sag, dropouts, azimuth error. Modern lo-fi (Mello-Fi, Wavesfactory
Cassette, our own `CassetteMachine`) is this deck aged twenty years. In-repo we already own a certified
cassette wobble: the **0.6 Hz ±2.0 ms primary + 2.2 Hz ±0.8 ms secondary + 7 Hz ±0.4 ms flutter**
triple-LFO stack with rate drift — ✅ **AUDIT-VERIFIED by reading the code**: spec comment
`TapeMachines.h:484`, **implementation `:582-598`** (primary = *triangle*, `slowRate = 0.6 + drift·0.2`;
secondary = sine, `2.2 + drift·0.5`; flutter = sine, `7.0 + drift·1.5`), `SmoothRandom` drift prepared at
`:514-516`, phases seeded at `:519-521`. ⚠️ *The old citation "`:483-516`" pointed only at the comment
block and the drift setup — it does NOT contain the LFO code; use `:582-598` when porting.* The
Distortion bible already recycles this stack for its Cassette character; we do too.

### 1.6 The modern DSP references
- **u-he Satin** (the DSP goldmine) — ⚠️ *NOT RE-VERIFIED in the 2026-08-14 audit: u-he.com returned
  HTTP 429 on two attempts. Every Satin figure below is the original researcher's claim, unconfirmed.
  Treat as design inspiration, never as quotable spec.* Tape speed **1.87→30 ips** continuous; service panel exposes
  Hiss, **Asperity**, Wow & Flutter, **Crosstalk**, **Bias**; repro-head **Gap Width / Bump / Azimuth**;
  circuit **HF compression** + Saturation; record/repro EQ standards **Flat · IEC 7.5 · IEC 15 · NAB ·
  AES 30**; **five compander models** (A-Type etc.); a Delay mode with **2 or 4 heads**,
  multiple-mono/cross/**ping-pong** routing and per-head motion; through-zero Flange mode; internal SR
  to 384 kHz. Satin proves the full physics can be a *user surface*; we compress it to 8 knobs.
- **ChowTape** (open source, DAFx-19 "Real-time Physical Modelling for Analog Tape Machines",
  J. Chowdhury): signal flow `In Gain → Filters → Tone(pre) → Comp → Hysteresis → Tone'(post) → Chew →
  Degrade → Wow/Flutter → Loss → Out` (manual Fig. 3). Hysteresis = Jiles-Atherton with selectable
  solvers (**RK2 / RK4 / NR4 / NR8 / STN**); Loss block = **Gap / Spacing / Thickness / Azimuth /
  Speed (3.75-7.5-15-30 ips)**; Degrade = Depth/Amount/Variance + **Envelope** (amplitude-envelopes the
  tape noise — their answer to our law 6); Chew = Depth/Frequency/Variance; **Flutter captured from a
  Sony TC-260**; Wow = Depth/Rate/Variance/**Drift**. The full JA math + the audio-normalised `cook()`
  constants are already transcribed and validated in-repo (`DISTORTION-BUILD-BIBLE.md` §9.1) — do not
  re-derive.
- **Strymon El Capistan**: five knobs (Time, Repeats, Mix, **Tape Age**, **Wow & Flutter**) + secondary
  functions on the same knobs + a 20 s sound-on-sound looper + three tape-machine head modes
  (fixed / multi / single-moving). ✅ **AUDIT-VERIFIED (2026-08-14)** against strymon.net/products/elcapistan
  FAQ: *"a 20 second sound-on-sound looper"*; secondaries map exactly — Time→**spring reverb**,
  Mix→**±3 dB boost/cut**, Tape Age→**low end contour**, Repeats→**tape bias**, Wow & Flutter→**tape
  crinkle**. ⚠️ *The three head-mode names were NOT on the support page (manual PDF only) — unverified.*
  The lesson: a
  beloved tape echo needs ~9 controls. Our chassis has 11. It fits.
- **Arturia Delay TAPE-201** (FX Collection — Max's requested deep reference): the RE-201 with
  3 heads + reverb tank in 12 combinations, **L/R / Ping-Pong / Mid-Side** operation, Repeat Rate
  syncable to host, input EQ (LP/HP/peak), and an advanced panel exposing **Flutter, Motor Inertia,
  Background Noise** + an LFO with 16 destinations. UI: VU meter, mode-selector dial with LED
  indicators. Motor Inertia as a *user parameter* is the confirmation our `Motor` knob (§4) is the
  right call.
- **AudioThing Outer Space**: 3 heads with per-head volume+pan, three tape types (original RT-1L /
  modern / **worn-out**), dropout simulation, low/hi-cut feedback filters, 2 spring tanks.
- **UA Galaxy Tape Echo**: mode selector, Tape Select, Echo/Normal switch, global Bass/Treble, pan per
  section — the "sound designer's RE-201".
- **ValhallaDelay / RE-202 / Satin delay mode** all converge on the same surface: heads/mode ·
  time+sync · repeats-to-runaway · wear/age · wow&flutter · sat · width · duck-or-EQ. Our back-8 (§4)
  is exactly this consensus.

### 1.7 The physics glossary — the numbers we build to
- 🛠️ **CORRECTED 2026-08-14 (audit): the wow/flutter split is 4 Hz, not 6 Hz.** ✅ **AUDIT-VERIFIED**
  against Wikipedia "Wow and flutter measurement": the terms are distinguished by *"wobbles at a rate
  below and above **4 Hz** respectively"*. So: **wow** = pitch modulation **< 4 Hz** (perceptually "tape
  warble"); **flutter** = **4-100 Hz** (roughness/graininess); **scrape flutter** = **>100 Hz**, from tape
  vibrating as it passes over a head — *"measured with a 10 kHz tone"* (both ✅ verbatim-verified);
  **drift** = <0.5 Hz slow wander. ✅ *"Listeners find flutter most objectionable when the actual
  frequency of wobble is **4 Hz**"*; ✅ *"Measurement is usually made on a **3.15 kHz** (or sometimes
  3 kHz) tone"*. ⚠️ **The 200 Hz claim was one-sided and is now stated in full:** the standard weighting
  curve *"presumes inaudibility of flutters above 200 Hz"* — but the same source immediately adds
  *"when actually faster flutters are quite damaging to the sound"*. Do NOT design as if >200 Hz is
  free; that is exactly why our scrape term (§3.2) is audible and env-gated, not decorative.
  Specs ✅ verified verbatim: professional machines *"around **0.02%**, which is considered inaudible"*,
  high-end cassette decks *"around **0.08%** weighted, which is still audible under some conditions"*.
  Design targets per Type in §2. (Nothing in §3.2's DSP changes — our wow LFOs sit at 0.6/2.2 Hz and the
  flutter terms at 7/23/45 Hz, cleanly either side of the corrected 4 Hz line. This was a *labelling*
  defect, and it mattered because the old text put our own 2.2 Hz "secondary wow" and 7 Hz "flutter"
  into the wrong buckets.)
- **Head/gap loss + bump**: playback head = a spatial aperture → HF loss `sinc(k·g/2)` shaped by gap
  `g`, spacing and thickness losses `exp(−k·d)`-type (ChowTape Loss block; full eq. 13 fit already in
  the Distortion bible: **`DISTORTION-BUILD-BIBLE.md:1435-1438`** — 3-biquad shelf fit, Sony TC-260
  reference d=20 µm g=5 µm δ=35 µm — ✅ in-repo reference confirmed by reading that file). **Head
  bump**: LF peaking, +2…+6 dB, Q≈1.2. ⚠️ *Provenance correction: this is a **transcribed model fit**
  (ChowTape eq. 13 + TC-260 geometry), NOT an in-repo bench measurement — the old wording "in-repo
  measured fit" overclaimed it.*
  🛠️ **PHYSICS CORRECTED (audit): head-bump frequency scales WITH tape speed, not inversely.** The bump
  (contour effect) is a fixed-**wavelength** phenomenon — the head's core geometry sets λ, so
  `f_bump = v/λ`, i.e. **f ∝ v**. Halving the speed HALVES the bump frequency. The old text (and the
  Distortion bible line it inherited) read "~45 Hz @15 ips, ~90 Hz @7.5 ips", which is the inverse law
  and cannot be right: **gap loss is the same fixed-wavelength physics** (`f_gap = v/g`, and §3.4
  already has that one as `f ∝ v`) — the two cannot scale in opposite directions off the same head.
  Corrected reference points: **~45 Hz @15 ips ⇒ ~22 Hz @7.5 ips ⇒ ~90 Hz @30 ips.** ⚠️ *Flag this
  back to the Distortion bible — `DISTORTION-BUILD-BIBLE.md:1438` still carries the inverted pair.*
- **Bias**: an inaudible HF carrier that linearises recording; under-bias = crossover-style "spitty"
  distortion + duller top, over-bias = cleaner but darker (Satin exposes it; ChowTape folds it into JA
  `k`). In THIS device bias is folded into per-Character sat voicing (§3.5) — the knob-level bias
  exploration belongs to Distortion.
- **Compander**: dbx/ANR-style compress-on-record + expand-on-play; mistracking = the lo-fi "pump".
  We already own a compander: `MoogDelay.h:51` (`Compander` struct) — recycled for `Deck`/`Cassette`.
- **Splice**: the loop's tape ends are joined once per loop pass → a periodic dropout+LF thump.
- **Motor inertia**: the capstan cannot step — speed changes are first-order glides; that is WHY
  changing Repeat Rate on an RE-201 bends pitch instead of zipper-jumping (§3.1 the glide law).

---

## 2. The Types — 7, each a different MACHINE  *(law 5: night-and-day or cut)*

Type dropdown order (default **Space**). Each Type states its **measurable discriminator** — the number
the harness (§9) must reproduce or the Type gets cut. Character dropdown = 8 entries per Type
(choice-param cardinality law fb342 ⑦ — keep every Character list the same length; Characters must
change PHYSICS, not EQ — the DST law).

### 2.1 `Space` — RE-201 multi-head loop ★ default
- **DSP:** full transport (§3.1) with **three read heads** at distance ratios **1 : 2 : 3** (head 3 =
  the Time knob's value; heads 1-2 land at ⅓ and ⅔ of it — musically the RE-201 dotted/triplet grid).
  Feedback taps the LONGEST ACTIVE head only (like the hardware — this is what makes patterns
  re-echo through each other instead of summing to mush). Preamp sat `S1` (§3.5), head EQ bump +3 dB
  @ **55 Hz·v** (🛠️ corrected — was `55 Hz·(1/v)`; bump tracks speed *directly*, §1.7/§3.4),
  gap-loss LP 7.2 kHz at v=1. Wow σ target 0.12% + flutter 0.05% (**at Wow ≈ 37 / Flutter ≈ 40** on the
  t^1.6 taper — the old text gave the σ with no knob position, which made it unfalsifiable in G2),
  **coherent across heads** (one motor).
- **Character (the 12-mode selector, reshaped to 8):** `Head 1` · `Head 2` · `Head 3` · `Heads 1+2` ·
  `Heads 2+3` · `Heads 1+3` · `All Heads` · `Swell` (all heads + the 4-stage allpass diffuser
  `TapeMachines.h:150` in the wet path at g=0.5 — the echo+reverb modes' wash without shipping a
  reverb).
- **Discriminator:** echogram tap count/positions — an impulse must show taps at exactly {⅓, ⅔, 1}·T
  per the Character mask, AND the per-tap pitch deviation must cross-correlate ≈ 1.0 across heads
  (one-clock proof). No other Type produces multi-tap + coherent wobble.

### 2.2 `Plex` — Echoplex EP-3 moving head
- **DSP:** ONE head. 🔑 Time moves the **head position d**, NOT the motor: `delay = d/v` with `d`
  glided (~80 ms τ), `v` untouched → a Time sweep produces a one-shot Doppler slur while moving, but
  the circulating loop does NOT re-pitch (opposite of Space — see discriminator). EP-3 preamp: `S1`
  voiced mid-forward (+2.5 dB @ 700 Hz pre-sat, matching the "EP-3 as a boost" lore), stronger
  per-pass darkening (gap-loss LP 5.5 kHz), cartridge wear on `Wear`.
- **Character:** `Fresh Cart` · `Aged Cart` · `Muddy Cart` (gap-loss 8k/5.5k/3.8k + rising dropout
  rate) · `Hot Input` / `Clean Input` (S1 pre-gain +8/−4 dB) · `Slow Slur` / `Fast Slur` (head-move τ
  240/30 ms) · `SOS` (sound-on-sound: input keeps writing OVER the loop at 25%, feedback forced 1.0 —
  env-gated so it still dies with the note, law 6).
- **Discriminator:** during a Time sweep, track pitch of a circulating impulse: `Space` re-pitches ALL
  existing repeats by `v_new/v_old` (sustained offset until re-recorded); `Plex` shows a transient
  Doppler excursion that returns to 0 cents. Sweep test in §9 measures exactly this trajectory.

### 2.3 `Drum` — Binson Echorec magnetic drum
- **DSP:** four heads at fixed ratios **1 : 2 : 3 : 4** (drum geometry — tighter, more "clock" than
  Space's loose loop). Wow/flutter floor: **wow σ ≤ 0.01%** regardless of the Wow knob's first 50%
  (the knob's top half bends the "drum bearing" instead — a 12 Hz micro-flutter, so the knob is never
  dead, law 5). Bandwidth HIGH: gap-loss LP 11 kHz (steel drum holds HF). Sat `S1` at half depth; hiss
  −6 dB vs Space. `Swell` regenerates all four heads through the diffuser with feedback cross-coupling.
- **Character:** `Head 1` · `Head 2` · `Head 3` · `Head 4` · `1+2` · `2+4` · `1+3` · `Swell`.
- **Discriminator:** pitch-deviation σ < 0.01% (vs ≥0.1% for every tape Type) at Wow=50, PLUS the 4th
  tap. The harness proves the drum by its *stillness*.

### 2.4 `Deck` — studio machine slap / ADT (the Deco lineage)
- **DSP:** one head, **Time floor drops to 0.3 ms** (Deco's Lag Deck spans −0.3→500 ms; we stay
  positive) and ceiling 500 ms free (sync divisions still available; > 500 ms hands over to `Space` —
  a Deck slap is not a dub delay). Clean transport: wow σ 0.03%, bandwidth 18 kHz, **compander in the
  loop** (recycle `MoogDelay.h:51`) — mistracking pump rises with `Wear`. **Wide mode** (Characters):
  the wet is duplicated with ANTIPHASE lag wobble L/R (Deco Wide Stereo) — instant double-track.
- **Character:** `Slap 15ips` · `Slap 7.5ips` (🛠️ **corrected** — bump **45 Hz +3 dB Q1.2** vs
  **22 Hz +6 dB Q0.8**, not "45 vs 90 Hz": bump frequency scales *with* speed, §1.7. The half-speed deck
  therefore moves its LF weight DOWN and makes it broader/louder rather than up — night-and-day is
  carried by that plus gap-loss 16k vs 12k) · `ADT` ·
  `Wide ADT` (±wobble antiphase) · `Cross` (feedback crosses L↔R) · `Invert` (wet polarity −1 —
  through-zero comb territory at sub-10 ms Time) · `Pump` (compander ratio hard) · `Loose` (lag wobble
  ×4).
- **Discriminator:** L/R lag cross-correlation: `Wide ADT` must show anti-correlated pitch deviation
  (ρ ≈ −1) — no other Type/Character in the whole FX rack does antiphase wobble. Plus the sub-ms Time
  floor (flange-adjacent comb visible in the magnitude spectrum).

### 2.5 `Cassette` — 1⅞ ips lo-fi
- **DSP:** one head + the **certified triple-LFO wow stack verbatim** (0.6 Hz ±2.0 ms, 2.2 Hz ±0.8 ms,
  7 Hz ±0.4 ms + SmoothRandom rate drift — impl `TapeMachines.h:582-598`, drift `:514-516`). Bandwidth
  ceiling: gap-loss LP **8 kHz → 4.5 kHz** as `Wear` rises; a **2 kHz +2.5 dB midrange resonance**
  (`:506`, `preBell`) — 🛠️ *NOT a head bump: the in-repo comment calls it "cassette head bump" but a
  1⅞ ips head bump lands near 10 Hz, not 2 kHz. It is a cabinet/head-shell midrange peak; keep the
  sound, fix the name so nobody derives it from the §3.4 speed law*; compander pump; dropout events
  from `Wear` (§3.8). Wow knob is ×4 the depth of Space's ⇒ **0 → ~2.4% σ** (🛠️ corrected — the old
  "~0.9%" contradicted §3.2's own 2.5% Cassette/Worn ceiling *and* its own "×4 of Space's 0.6%").
  On the t^1.6 taper the audible **0.08%** zone is crossed at knob **≈12%** (🛠️ was "~15%";
  `(0.08/2.5)^(1/1.6) = 0.116`).
- **Character:** `Type I` · `Type II` (sat knee later+harder, LP 9.5k) · `Ferric Worn` · `Walkman`
  (motor sag: v dips −1.2% under program peaks — level-dependent pitch, the dying-battery tell) ·
  `Boombox` (bump moved to 120 Hz +4 dB, LP 6k) · `Dictaphone` (band 300 Hz-3.2 kHz + S1 hard) ·
  `Micro` (v=0.92 fixed — everything slightly flat + slow) · `Chrome` (clean: wow ÷2, LP 11k).
- **Discriminator:** HF-ratio collapse (energy >6 kHz at least −18 dB vs `Deck`) + wow depth an order
  of magnitude above `Space` + audible dropout events ≥ 0.2/s at Wear 60.

### 2.6 `Worn` — the dying machine (event machine, fb325 law)
- **DSP:** `Space`'s transport, but every fixed constant becomes a **certified slow random WALK**
  (Phase G Worn-walk law: a per-sample noise smoother is NOT a walk — use the fb345-certified
  walk: target-resample at 0.3-1.2 Hz + cubic ease): gap-loss corner walks 3-9 kHz, azimuth walks
  (independent L/R HF tilt ±3 dB above 4 kHz), head bump gain walks 0-6 dB, dropout+crinkle event
  machines always armed (§3.8), splice thump ×3 prominence, hiss +6 dB (still env-gated). Feedback
  path gains walk ±1.5 dB (repeats breathe).
- **Character:** `Shedding` · `Crinkle` · `Dropout City` · `Azimuth Drift` · `Splice Storm` ·
  `Baked` (everything dark + slow: v=0.97, LP 4k) · `Moldy` (walk rates ×3) · `Last Play` (all of it,
  plus a −0.5%/min v decay that resets per note-on — the machine dies WITH the phrase, law 6).
- **Discriminator:** nonstationarity — 10 s spectral-flux variance ≥ 5× any other Type at identical
  settings; dropout-event count ≥ 1/s at Wear 50. The harness must measure the *variance of* the
  metrics, not the metrics (probe-craft law fb345: bias/walk axes need a time-variance metric).

### 2.7 `Reels` — varispeed performance machine  *(the cut candidate — §12 Q3)*
- **DSP:** `Deck`'s clean transport; Character selects a **motor EVENT SHAPE** triggered by the `Stop`
  pill (and re-armed per note-on): momentary speed trajectories through the SAME v-glide law (§3.1),
  so everything (echoes + the wet dry-through) pitches as one machine.
- **Character:** `Brake Slow` (v→0 over 1.8 s) · `Brake Fast` (280 ms) · `Rev Spool` (v→−1: true
  reverse read of the loop; input muted while reversed) · `Pump ¼` / `Pump ⅛` (tempo-synced ±30% v
  wobble — synced through the 4-bar→1/256 grid, law 3) · `Drop Oct` (v→0.5 glide 400 ms) ·
  `Rise Oct` (v→2.0) · `Chaos` (SmoothRandom v ±40%, env-gated so it free-runs NEVER, law 6).
- **Discriminator:** the pitch trajectory itself — a measured v(t) curve per Character (brake = exp
  decay to 0 with τ from `Motor`; pump = synced sine). No other device in Terrain can pitch its own
  echo history.
- **If cut to 6 Types:** `Brake Fast` becomes the global `Stop` pill behavior for every Type (already
  planned), `Rev Spool` and the pumps die, `Drop/Rise` fold into Character slots on `Deck`. Nothing
  else is lost — which is exactly why it is the cut candidate.

---

## 3. DSP core — the transport, the loop, the laws

### 3.1 🔑 THE TRANSPORT — one motor clock, everything derives from it

State: virtual speed `v` (1.0 = nominal), head distances `d_i` in samples-at-nominal.

```
delay_i(t) = d_i / max(v(t), v_floor)         // taps move when the motor moves;  v_floor = 0.03
v̇ = (v_target − v)/τ_motor                    // motor inertia: FIRST-ORDER GLIDE, never a step
τ_motor = 0.03 + 1.47·(Motor/100)²  seconds   // knob law: 30 ms (tight) → 1.5 s (flywheel)

d₃_nominal = 600 ms · fs        // ⚠️ MUST be stated: head 3's distance at v = 1. Everything below scales off it.
v_target(Time) = clamp( d₃_nominal / T , 0.25 , 4.0 )       // the MOTOR band: ±2 octaves of speed
```
🛠️ **AUDIT-ADDED — the v-routing had no stated range, and without one it is unbuildable.** The old text
gave `v_target = d₃_nominal / T_new` with a free Time range of **1 ms → 16000 ms**. With any plausible
`d₃_nominal` that demands a motor spanning four orders of magnitude (T = 1 ms ⇒ v = 600; T = 16 s ⇒
v = 0.0375) — not a capstan, a fantasy, and it drags the wow/flutter/splice rates (all ratio-locked to
`v`, §3.2/§3.7) along with it. The law:

- **Inside the motor band (v ∈ [0.25, 4], i.e. T ∈ 150 ms … 2400 ms):** pure motor. This is where every
  musically normal Time move lives, and it is where the §2.2 pitch-bend signature happens. Untouched.
- **Outside it:** `d₃` **re-seats in octave steps** (×2 / ÷2) to pull `v` back inside the band, under a
  **40 ms output crossfade** (law 4/7 — a re-seat is a delay-length jump and WILL click otherwise).
  Re-seats are silent to the user except that a Time move spanning an octave boundary bends, then
  re-centres. State the re-seat count in the viz (§5.2) so it is never a mystery.
- **`v_floor = 0.03` and the `max()` are not decoration:** the `Stop` pill drives `v_target → 0` and
  `Reels/Rev Spool` drives it **negative**. `d_i / v` is a division by zero at Stop and returns a
  *negative delay* under reverse — both are hard faults in the old formula. During spin-down the tap
  length grows toward `d_i/v_floor`; **fade the tap out once `delay_i > 0.95 · bufferLen`** (the tape has
  physically stopped delivering material to that head — the echo glides down in pitch and dies, which is
  what a real machine does). Reverse read (`v < 0`) is a **separate read path**, not this formula
  (§2.7 / P11 already mute the record path there).

- **The pitch bend is FREE:** a delay-line tap whose length changes at rate `ṙ` reads the signal
  resampled by `1 − ṙ` — gliding `v` IS the Doppler. No pitch-shifter needed for Repeat-Rate bends,
  tape-stop, or `Reels` events. (The MoogDelay pitch-in-feedback machinery `MoogDelay.h:306-398` is
  for *constant* pitch offsets — different tool; do not import it here.)
- **Sync + Time (law 3):** synced range **4 bars → 1/256** — reuse the fb306 20-entry `SYNCDIV` list
  and the DLY host-resolve grammar verbatim. ✅ **AUDIT-VERIFIED:** the list really is 20 entries and
  really spans 4 bars → 1/256 — `PluginProcessor.cpp:3456-3459` (`"Free","4 bar","2 bar","1 bar","1/2",
  "1/2D","1/2T","1/4","1/4D","1/4T","1/8","1/8D","1/8T","1/16","1/16D","1/16T","1/32","1/64","1/128",
  "1/256"`, default index 10 = 1/8), param `ParameterIDs.hpp:376`, UI mirror `index.html:7485`.
  ⚠️ *The comment on `ParameterIDs.hpp:376` is STALE ("Free/1-4/1-8/1-8T/1-8D/1-16") — trust the
  StringArray, not the comment.* Free range 1 ms → 16000 ms (`DelayEngine.h:84` precedent; buffer sized
  like `DelayEngine.h:40` — `ceil(16.5·fs)+8` rounded UP to a power of two).
  🛠️ **Memory corrected:** at 48 k that is `2²⁰ = 1 048 576` samples/ch ⇒ **8.4 MB stereo float**
  (2 × 4 MiB), *not* the "~5.3 MB" the old text claimed (which is neither the raw 16.5 s figure, 6.3 MB,
  nor the pow-2 one). ⚠️ **At 96 k it rounds to `2²¹` ⇒ 16.8 MB** — budget for it in `prepare()`.
  ONE shared buffer for all heads; heads are just extra read taps: ≈ free.
- **Time changes route through v (Space/Drum/Cassette/Worn/Reels) or through d (Plex/Deck)** — this
  single routing switch IS the §2.2 discriminator. When routed through `v`, the motor re-speeds so head 3
  lands on the new Time (`v_target` per the clamped law above); `d_i` never changes *except* at an
  octave re-seat. When routed through `d`, `d` glides at τ_head (Character) and `v` stays 1 — and because
  `v` stays 1 the d-routed Types need **no motor band and no re-seat**: `d` alone covers 1 ms → 16 s.
- 🔑 **STRANDED-CLOCK / ONE-CLOCK LAW (fb345):** wow, flutter, splice position, and the `Reels` event
  shapes ALL phase-accumulate off the SAME `v` integrator (`tapePos += v` per sample; LFO phases
  advance by `rate·v·T`). Separate free-running phase accumulators integrate glide skew and drift
  apart — the certified one-clock law. One integrator, everything reads it.
- **Interpolation:** cubic Hermite read (recycle `DelayEngine.h:readAt` `:230-249` or
  `TapeMachines.h:280` `readCubic` — identical math). Linear fallback tier only if CPU demands (§8).

### 3.2 Wow / flutter / scrape — the modulation generator (per Type depths in §2)

All three modulate `v` multiplicatively: `v_eff = v · (1 + w(t))`.

```
w(t) = Wow%   · [ 0.62·sin(2π·0.6·φ) + 0.25·sin(2π·2.2·φ + drift) + walk(0.4 Hz) ]     // wow  < 4 Hz
     + Flut%  · [ 0.20·sin(2π·7·φ) + 0.12·sin(2π·23·φ) + 0.08·bp(noise, 45 Hz) ]        // flutter 4-100 Hz
     + Flut%² · 0.03·bp(noise, 2.8 kHz)·envScrape                                       // scrape  >100 Hz
φ advances with v (one-clock law).  bp() = SVFBandpass (TapeMachines.h:188 — ✅ verified), Q≈2.
```
🛠️ *Band labels corrected: the wow/flutter split is **4 Hz**, not 6 Hz (§1.7, verified). The DSP is
unchanged — 0.6/2.2 Hz are wow, 7/23/45 Hz are flutter, either side of 4 Hz. Only the comments moved.*

- Knob → physical depth law (taper t^1.6 so the top half is where the drama lives, law 5):
  `Wow 0→100 ⇒ σ_pitch 0 → 2.5%` on Cassette/Worn, `0 → 0.6%` on Space/Plex, `0 → 0.15%` on
  Deck, drum-floor rule on Drum (§2.3). 100% = seasick — *just past useful* (no playing safe).
  Reference points: 0.02% pro / 0.08% cassette-audible / 4 Hz most-objectionable (§1.7).
  🛠️ **Taper arithmetic corrected:** with `t^1.6` and a 2.5% ceiling, knob **25% lands at σ ≈ 0.27%**
  (`0.25^1.6 = 0.109`), not the "~0.1%" the old text claimed — the two numbers were inconsistent with
  the stated exponent. 0.27% is ~3.4× the audible threshold, so the no-plateau claim survives *more*
  strongly; the audible 0.08% line is crossed at knob **≈12%**.
- `walk()` = the certified fb345 walk (target resample + cubic ease), NOT a smoothed noise (Worn law).
- Scrape is gated by `envScrape` = input envelope (10 ms attack / 180 ms release, squared-release law) —
  scrape is stick-slip: no program, no friction, no sound (law 6).
- **Stereo:** one shared `w(t)` (one transport) EXCEPT `Deck/Wide ADT` (antiphase, §2.4) and Worn's
  azimuth walk (filter-domain, not time-domain).

### 3.3 The loop — every gain stage named (LOOP GAIN LAW, law 6)

```
input → Duck-sense ┐
x = in + fb_signal │
S1 preamp sat (§3.5, driveDb = 30·t^0.8 dB, ADAA-1 + 2× local OS) → record EQ (pre-emph +3 dB @3 kHz, TapeMachines.h:422 ✅)
 → WRITE tape[wr]
READ head_i (Hermite, delay_i) → playback EQ_i: bump biquad (+B dB @ f_bump/v) → gap-loss LP (f_gap·v)
 → pattern mix Σ g_i (Character mask, g_i = 1/√N_active)     → WET out (post: hiss + width + duck)
feedback tap = LAST active head → Wear color (per-pass LP/dropout) → AC-couple HP 22 Hz
 → fb gain (Repeats: 0 → 1.15) → LIMITER (in-loop, §below) → back to x
```

Gain audit of the loop (worst case, Repeats=1.15): bump peak +6 dB (×2.0) is OUTSIDE the feedback tap
only if we tap PRE-playback-EQ — **we tap POST head EQ** (the hardware truth: repeats darken AND
thump), so the audit is: `fb 1.15 × bump 2.0 × gapLP ≤1 × S1 smallsignal ≤1.05 × HP ≤1 = 2.42` at the
bump frequency → **unstable without the limiter — the limiter is MANDATORY, not a color**:

- **In-loop limiter:** zero-lookahead soft-knee `y = x/(1+|x_env|/k)` with `k = 0.16` linear
  (= **−15.9 dBFS knee ≈ +10 dB over the −26 dBFS program**, law 1 — a 0 dBFS-referenced knee would
  NEVER engage on our bus and the "bounded runaway" would be a hard-clip scream). Envelope 5 ms/60 ms.
  Zero lookahead ⇒ **no reported latency** — rack law A safe (§6).
- 🛠️ **PARK LEVEL — AUDIT-CORRECTED. The old number came from an invalid approximation.** The old text
  said "past the knee the effective gain is `k/|x|` → loop gain falls below 1 at `|x| ≈ 2.42·k ≈
  −8 dBFS`". `k/|x|` is the *asymptotic* limit of `1/(1+|x|/k)`, valid only for `|x| ≫ k` — and the
  answer it produces is `|x| = 2.42k`, i.e. only 2.4× k. The approximation is being used exactly where
  it does not hold, and it overstates the park level by **~4.7 dB**. Solve it exactly instead:

```
loop:      x_{n+1} = L(G·x_n),   L(u) = u / (1 + |u|/k),   G = 2.42,  k = 0.16
fixed pt:  G / (1 + G·x*/k) = 1   ⇒   G·x* = (G − 1)·k
PARK (measured at the limiter input, i.e. post-bump, the hottest point in the loop):
           |x|_park = (G − 1)·k = 1.42 × 0.16 = 0.227  =  −12.9 dBFS
stability: d/dx[ L(Gx) ] at x* = 1/G = 0.41 < 1   ⇒   the park point is an ATTRACTOR, not a knife edge
```

- **Max stable loop gain: unbounded-input-bounded-output for any Repeats ≤ 1.15.** The self-osc wash
  parks at **−12.9 dBFS ≈ +13 dB over the −26 dBFS program** (🛠️ was "−8 dBFS / ~18 dB over") and STAYS
  there. Put `(G−1)·k` in the code as the formula, not the constant — `G` moves whenever the bump
  ceiling or Repeats ceiling is re-voiced, and a hard-coded park level silently goes stale.
- **Second bound, free:** S1's `tanh` also lives in the loop, so even with the limiter defeated the
  circulating signal is bounded by the tanh asymptote. The limiter is what makes the runaway *musical*
  (it parks instead of squaring off) — it is still MANDATORY, but the BIBO claim does not rest on it
  alone. Note at Drive = 0, S1 ≈ identity and tanh only bites near 0 dBFS, so the limiter carries the
  whole job at the default — which is precisely the case that matters.
- **Env-gate the runaway (law 6):** fb_signal is multiplied by `g_note = smoothstep(env_in)` with a
  **12 s release** — self-osc sustains long after note-off (the dub move) but is guaranteed to die;
  at gate closed the loop input is zero and the wash decays at `fb^(t/T)`. Nothing free-runs.
- **AC-coupled loop (Phase G law):** the 22 Hz one-pole HP inside the loop kills DC latch-up from S1's
  asymmetry — the exact silence-class that murdered 5 Distortion presets. Non-negotiable.
- Denormals: `flush()` on the write (grammar of `DelayEngine.h` `flush`) + `ScopedNoDenormals`.

### 3.4 Head EQ math (all coefficients cached, recomputed only on param/v-glide epochs)

```
ṽ = clamp(|v|, 0.05, 4.0)                              // ⚠️ EVERY head-EQ coefficient uses ṽ, never raw v

Bump:    peaking biquad, f = f_bump0 · ṽ               (bump tracks SPEED *directly*: 45 Hz @15ips → 22 Hz @7.5ips)
         gain = Bump knob → 0…+6 dB (t^1.2), Q = 1.2   (model fit — Distortion bible DISTORTION-BUILD-BIBLE.md:1435-1438)
Gap loss: one-pole LP, f_gap = f_type · ṽ              (half-speed tape = half the bandwidth — free realism)
Azimuth (Worn/Cassette): 1st-order HF shelf, L/R gains split ±walk — cheap, no FIR
```
🛠️ **TWO AUDIT FIXES HERE.**
1. **`f = f_bump0 · (v_nominal/v)` was inverted** — see §1.7. Bump and gap loss are the *same*
   fixed-wavelength physics off the *same* head; they cannot scale in opposite directions. Both are now
   `∝ v`. (Bonus: this also kills a latent bug — under the old inverse law, `Stop` (v→0) sent the bump
   frequency to **infinity** and blew the biquad coefficients up.)
2. **The `ṽ` clamp is new and is not optional.** `Stop` drives v→0 and `Reels/Rev Spool` drives it
   **negative**; an unclamped `f = f0·v` yields a zero or *negative* corner frequency, which is a
   coefficient NaN, which under §3.10's rule is forever. Clamp to `[0.05, 4.0]` and take `|v|` — during
   a tape-stop the EQ simply parks at its darkest, which is also what the ear expects.

Do NOT run the 100-tap eq-13 loss FIR — the 3-biquad shelf fit is the certified route.

### 3.5 The saturation stage S1 — deliberately NOT hysteresis (the ChowTape verdict)

**Verdict: no Jiles-Atherton in this device.** Reasons, in order: (1) the JA sound (level-dependent
transient smear) already ships in Distortion ANALOG/Tape at 4× OS — duplicating it doubles CPU for a
sound the user can chain in one click; (2) the echo loop passes the SAME signal through S1 dozens of
times — hysteresis-in-a-loop compounds its own smear into mud (verified subjectively by every
hardware-modeler that puts *light* sat in the loop: RE-202 exposes a simple "Saturation" control, not a
tape model); (3) our CPU budget (§8) buys 4 heads + transport + events for the price of ONE hysteresis
channel. The machine's identity is the transport; the magnetisation is Distortion's identity (§0.2).

```
S1(x) = tanh(g·x + b·(g·x)²·sign-safe) / n_char        // memoryless, asym via b (even harmonics)
driveDb = 30 · t^0.8   (t = Drive knob 0..1)           // HOUSE DRIVE LAW on the −26 dBFS bus (law 1)
b = 0.05…0.14 per Character (the bias voicing — under-bias "spit" lives in Plex `Hot Input`,
     Cassette `Dictaphone` via a small dead-zone term: x −= clamp(x, ±dz), dz ≤ 0.04)
n_char = FIXED per Character (measured unity constant — NEVER program-dependent; the Distortion
     bible's Tape normalisation law: a program-dependent normaliser erases the level-dependence)
```
- At Drive=0: g = 1.0, S1 ≈ identity (±0.05 dB) → **unity-through default** (§6).
- 🛠️ **D_max CORRECTED +36 → +30 dB.** The house drive law is `driveDb = D_max·t^0.8`
  (`DISTORTION-BUILD-BIBLE.md:190`) and its **D_max table at `:199-206` gives ANALOG = +30 dB**; +36 dB
  is FOLD's figure. A stage the bible itself calls "a cheap memoryless soft stage… deliberately lighter
  than Distortion's Tape" cannot be voiced **hotter** than the whole ANALOG family. +30 dB on −26 dBFS
  program still peaks at **+4 dBFS into the tanh** (well past the knee, compounding every loop pass) —
  no playing safe, and it lands exactly on the house class.
- 🛠️ **ALIASING VERDICT — THE OLD ONE RESTED ON A MISQUOTE. CORRECTED.** The old text claimed the
  Distortion bible's "master rule" is that *"memoryless soft sat at this drive class = no oversampling,
  no ADAA needed"*. **It says no such thing.** Read for this audit:
  - `DISTORTION-BUILD-BIBLE.md:322-360` — the actual master rule is a *strategy-efficiency* measurement
    (2×+ADAA-1 beats 4× naive at 72% of its CPU), not a permission to skip AA.
  - Its **authoritative per-mode budget at `:439-461`** puts **`Soft Clip` — the tanh-class memoryless
    sigmoid, our exact case — at `ADAA-1 + 2×` as its *floor*** ("A mode declares its floor; the
    `Quality` dropdown may only **raise** it. Never lower."), and flags: *"⚠️ At high drive ALL sigmoids
    converge to `sign(x)` — above ~+30 dB the floor is set by the DRIVE, not the curve."*
  - `:429-430` — *"ADAA must never run at 1× (it audibly dulls)"*. So "ADAA only, no OS" is also refused.

  **Therefore S1's floor is `ADAA-1 + 2×`, matching Soft Clip.** Specifics:
  - **Only the memoryless S1 stage is oversampled**, locally (up → tanh → down), *before* the tape
    write. The transport, the delay taps, the head EQ and the feedback recursion all stay at **1×** —
    you cannot and need not oversample a recursion to anti-alias a memoryless stage sitting in front of
    the write.
  - **ADAA-1 is already in this tree, audited: `TerrainFilters.h:990-1030`, `struct WaveShaper`** —
    including both fallback branches (`|Δx| < 1e-5` → midpoint, `|Δx| > 0.9` → `f(x)`). Its own header
    comment reads *"1st-order antiderivative anti-aliasing (ADAA) + host 2x oversampling"* — the exact
    pairing. **Lift it; do not write a new one** (law 10).
  - **Law A is safe.** `juce::dsp::Oversampling` polyphase-IIR at 2× measures **3.14 samples** latency at
    base rate (DST bible `:462-465`). That delay sits *inside* the echo loop, where it is absorbed into
    the tape delay (3 samples on a ≥150 ms tap = 0.002%, inaudible) and is **never reported to the
    host** — so the fb305 exclusion sums stay sample-aligned. Do NOT call
    `setLatencySamples()` for it.
  - What the loop genuinely *does* buy us: the per-pass gap-loss LP strips the top octave every trip, so
    aliasing does not accumulate across repeats. That argument is sound — it was just being asked to do
    the work of the *first* pass as well, which it cannot.
  - **Quality tiers (§8):** Standard `ADAA-1 + 2×` · High `ADAA-2 + 2×` · Ultra `ADAA-2 + 4×`.
    Eco may drop to `ADAA-1 + 2×` with linear taps. **The old "ZERO oversampling at every Quality tier"
    claim is withdrawn.**

### 3.6 Hiss + asperity — env-gated (law 6, Phase G silence-class compliant)

```
hiss = Hiss% · [ white·0.5 + bp(white, 3.1 kHz, Q 0.7)·0.5 ]      // asperity = mid-weighted noise
out += hiss · env_in(atk 8 ms, rel 250 ms, SQUARED release) · ṽ    // dies with the note; speeds down with tape-stop
```
🛠️ *`· v` → `· ṽ` (the §3.4 clamped `|v|`). Under `Reels/Rev Spool` raw `v` is **negative**, which would
have flipped the hiss polarity — audible as a click at the reversal, and nonsense physically.*
Knob law: 0 → −∞, 50 → −46 dBFS, 100 → **−30 dBFS** under nominal program (obvious old-machine floor —
dramatic, not polite; the bus sits at −26 dBFS so this is program −4 dB: unmistakable, law 5/9).
LCG noise (`rng` grammar `DelayEngine.h:46`), not std::mt19937, on the audio thread.

### 3.7 Splice — the loop has a seam

```
splicePhase += v / (3.1 · fs)          // ⚠️ ACCUMULATE, never "period = 3.1 s / v"
if (splicePhase >= 1) { splicePhase -= 1; fireSeam(); }        // v<0 ⇒ phase runs backwards, seam still lands
event: 6 ms half-cosine dip to −3.5 dB  +  LF thump: one cycle of 55 Hz sine at (−34 dBFS · env_in)
```
🛠️ *The old `splicePeriod = 3.1 s / v` **divides by zero at `Stop`** and goes negative under
`Rev Spool` — and it silently contradicted the sentence right next to it ("ratio-locked to the
transport, one-clock law"). A period computed from an instantaneous `v` is exactly the stranded-clock
mistake P2 warns about: it is only correct while `v` is constant, which is never, since the whole device
is built on gliding `v`. The phase accumulator above IS the one-clock law — at v = 0 the seam simply
stops arriving, which is what a stopped tape does.*
Thump is env-gated (law 6). `Worn/Splice Storm`: period ÷ 4, dip −8 dB, thump +8 dB. On `Drum`: NO
splice (no seam on a drum — another §2.3 stillness tell). Splice intensity rides the `Wear` knob
(0 → seamless new loop, 100 → every pass audible).

### 3.8 Dropouts + crinkle — event machines (fb325 law: events, not noise)

Poisson-scheduled events, rate armed by `Wear` and only while `env_in > −60 dBFS` (law 6):
```
dropout: rate 0…8/s (Wear t^2), depth −2…−20 dB, width 5…40 ms, half-cosine, per-channel independent
crinkle (Worn only): rate 0…3/s — a 12…30 ms segment where v_eff wobbles ±1.5% at 60 Hz (the ChowTape
         Chew reduced to a v-domain event: no extra buffer, rides the same tap)
```

### 3.9 Param glide table (law 7 — every param, its smoothing, stated)

| Param | Glide | Why |
|---|---|---|
| Time (v-routed) | via τ_motor (30 ms–1.5 s) | THE feature — pitch-bends, never zips |
| Time (d-routed: Plex/Deck) | τ_head 30–240 ms exp | comb-click law; Doppler slur is the Plex tell |
| Repeats | 15 ms one-pole | loop gain must not step |
| Drive | 15 ms + S1 output crossfade on Character switch | Phase G deferred-fade law |
| Wow/Flutter/Wear/Hiss/Bump/Width/Duck | 15 ms one-pole (`smCoef` grammar `DelayEngine.h:57`) | anti-zipper |
| Type switch | 75 ms dual-run crossfade (recycle `TapeProcessor.h:80-102` pattern verbatim) | law 4: never cuts |
| Character switch | same-engine re-voice + 40 ms output crossfade + **re-seat** (fb345 char-switch law) | no boot-quiet trap (P6 law) |
| Stop pill | attack: v_target=0 through τ_motor·0.4; release: spool-up τ_motor·1.6 | asymmetric like real motors |

### 3.10 Stability + hygiene summary
In-loop: HP 22 Hz (DC), limiter knee −16 dBFS, hard cap ±1.5 (`DelayEngine` cap grammar), flush()
denormals, `isfinite` guard on the feedback sample (NaN in a loop is forever). All Character masks
normalize `1/√N` so switching head counts never jumps level. Buffer clear on `reset()` +
`prepare()` re-entry (auval SIGBUS lesson fb344: never trust stale allocations across SR changes).

---

## 4. Chassis map — the fb275 card

Device prefix `SYN_TPE_*` (`TAP` collides visually with tape-loop; `TPE` matches the DLY/RVB/DST
grammar — ✅ verified: `SYN_RVB_*` block starts `ParameterIDs.hpp:345`, `SYN_DLY_*` at `:374`,
`SYN_DST_*` at `:406` and runs past `:428`; the old "`:345-419`" range stopped inside the DST block).
Routing pills SRC_A/B/C/D/SUB/NOISE + SYNC/POWER exactly per the DLY precedent — ✅ verified
`ParameterIDs.hpp:389-397`.

🛠️ **Param-count label corrected.** The old heading said "11 params" and then listed 16. The fb275
chassis is a **shape**, not a total: **2 dropdowns + 8 back knobs (4×2, three separators)** is what the
back panel must be, and the front carries **3 hero knobs + Mix + pills**. Counting this device honestly:
4 front knobs (Time/Repeats/Drive/Mix) + 8 back knobs + 2 dropdowns + `SYNCDIV` + 2 front pills
(Sync/Stop) + Power + 6 routing pills. Matching DLY exactly.

### 🔑 LAW C — choice cardinality is fixed AT BIRTH (fb342). Read before §12.
JUCE/VST3/AU cache the parameter list at construction; an `AudioParameterChoice`'s option count can
**never** change afterwards, in either direction — you cannot grow a Type list *or shrink it* in a later
build without breaking every saved preset's index mapping. Therefore:
- **`SYN_TPE_TYPE` must be created with its FINAL roster on day one.** §12 Q3 asks whether `Reels` gets
  cut to leave 6 Types — that question **must be answered before the parameter exists**, and if the
  answer is ever "maybe later", the list ships at **7 with `Reels` disabled**, never at 6.
- **`SYN_TPE_CHARACTER` likewise.** §12 Q2 floats 8 vs the authentic 12 for `Space`. If 12 is even
  *possible*, the param is born with **12 slots** and the short Types simply grey out slots 9-12 —
  choosing 8 now forecloses 12 forever.
- Nothing here is created at runtime (law B): all 7 Types × 12 Character slots are a **pre-allocated
  fixed list**, exactly as `SYN_DST_TYPE`'s `choice(23)` is (`ParameterIDs.hpp:406`).

### Front card (3 + Mix + pills + viz)
| Control | Param | Range / taper | Notes |
|---|---|---|---|
| **Time** | `SYN_TPE_TIME` + `SYN_TPE_SYNCDIV` | synced **4 bars → 1/256** (fb306 20-entry list) / free 1–16000 ms, log taper | glide per §3.9 — this knob PLAYS |
| **Repeats** | `SYN_TPE_REPEATS` | 0 → 1.15 loop gain, t^0.9 | >1.0 zone marked on the arc (self-osc) |
| **Drive** | `SYN_TPE_DRIVE` | 0 → **+30 dB** via 30·t^0.8 | law-1 calibrated; D_max = the house ANALOG figure (§3.5) |
| **Mix** | `SYN_TPE_MIX` | equal-power, **100% = fully wet** | law 4 |
| Pills | `SYN_TPE_SYNC` (default ON) · `SYN_TPE_STOP` (momentary; latched = param true while held) | | Stop = the demo moment |

### Back panel — 2 dropdowns + 8 knobs (4×2, three separators)
| Slot | Name | Param | Law |
|---|---|---|---|
> 🔧 **[CROSS-BIBLE AUDIT 2026-08-14] CHASSIS CORRECTION — `Type` is the HEADER PILL, not back-d1.**
> Verified in the shipped tree: on Reverb, Delay **and** Distortion, `*_TYPE` renders in the header
> `.fxr-type` `<select>` on the card centerline (`index.html` `DEVS[].tp` +
> `Design/fx-back-panel-mockup.html`); the two **back** dropdowns are `Character` + a second
> selector (`Mod Mode` / `Sync` / `Quality`). Spending back-d1 on `Type` duplicates the header pill
> — the most visible label the card has — and silently throws away a back dropdown this device is
> entitled to. Move `Type` to the header, slide `Character` to back-d1, and back-d2 is free.
> Full ruling (incl. that the honest knob count is **12** = 3 heroes + Mix + 8 back, not the "11"
> four bibles reconstructed four different ways): `FX-CHAIN-BIBLE.md` §7.1.

| Dropdown 1 | **Type** | `SYN_TPE_TYPE` | choice(7) §2 |
| Dropdown 2 | **Character** | `SYN_TPE_CHARACTER` | **choice(12) at birth**, 8 used per Type today (law C above) — head patterns on Space/Drum, machine voicings elsewhere |
| K1 | **Wow** | `SYN_TPE_WOW` | §3.2, t^1.6, per-Type depth scale |
| K2 | **Flutter** | `SYN_TPE_FLUTTER` | §3.2 (+ scrape in top half) |
| K3 | **Wear** | `SYN_TPE_WEAR` | age master: gap-loss walk-down + dropouts + splice + compander pump (§3.4/3.7/3.8) |
| K4 | **Hiss** | `SYN_TPE_HISS` | §3.6, env-gated, −∞ → −30 dBFS |
| K5 | **Motor** | `SYN_TPE_MOTOR` | τ_motor 30 ms → 1.5 s, t² (§3.1) — also scales Stop/`Reels` event times |
| K6 | **Bump** | `SYN_TPE_BUMP` | head-bump 0 → +6 dB @ speed-tracked f (§3.4) |
| K7 | **Width** | `SYN_TPE_WIDTH` | 0 → 1.6 M/S on WET only (`DelayEngine.h` width grammar) + per-head pan spread on multi-head Types |
| K8 | **Duck** | `SYN_TPE_DUCK` | input-ducking 0..1 (recycle `DelayEngine.h:218-224` verbatim — proven loved) |

Pragmatic-name check (fb144): every name says what it does — Wow wows, Wear wears, Motor is the
motor, Bump bumps. No jargon survives ("azimuth", "compander", "bias" all live INSIDE Wear/Character
voicings, not on knobs). Title-case, no fake acronyms (SOS is a real one).

---

## 5. Visualizers — survey, then our card

### 5.1 How the greats show tape
- **Arturia TAPE-201:** skeuomorphic panel — mode-selector dial with **LED indicators per mode**, a
  **VU needle** for level, dry/wet fader. The machine metaphor carried by lights + meter, not motion.
- **AudioThing Outer Space / UA Galaxy:** photoreal RE-201 face — the drama is the panel itself;
  head activity implied by the mode dial only. No live signal viz.
- **ChowTape:** engineering UI — static curve plots in the manual (hysteresis loops, loss curves),
  the plugin itself is knobs + meters. Educational, not reactive.
- **u-he Satin:** tape-path diagram + VU meters; the service panel is a parameter surface, the "viz"
  is metering.
- **Wavesfactory Cassette:** an animated cassette — **spools spin at tape speed, wobble with wow** —
  the one commercial tape UI where the *physics is the animation*. This is the direction that fits
  Max's law 9 (and we already prototyped it: `Design/cassette-viz-preview.html` — ✅ verified, the spool
  drawing is **canvas-2D JS at `:125-221`**, not markup, and note it calls `createRadialGradient` **per
  frame** at `:141`, which the fb342 push-lane law makes us hoist to a cached gradient — plus
  `space-viz-options.html`, `studio-viz-preview.html`,
  `wire-viz-preview.html` from the DLY-panel tape section work).
- **Serum 2:** n/a — no tape effect (the differentiator). Its FX-viz house style (a live plot that IS
  the parameter state) is still the bar for reactivity.

### 5.2 Our card — 3 concepts (canvas, 60 Hz push-lane laws fb342: no per-frame shadowBlur/filters, visible×fresh pushes only)

**A. `Transport` ★ recommended** — a stylized side-view tape path: two spools, a head bridge with
1-4 head dots (lit per Character mask), the tape as a poly-line. Mechanics: spool rotation integrates
`v` — 🛠️ *the old text said "via the existing viz-poll native" without naming one, and **no tape viz
native exists**. Verified: the FX-viz grammar in this tree is `getReverbBloom`
(`PluginEditor.cpp:4862`) and `getDelayBloom` (`:4885`), plus `DelayEngine::getFeedbackViz()`
(`DelayEngine.h:226`). **Add `getTapeTransport`** on that exact pattern, returning
`{v, tapePos, splicePhase, headGains[4], fbFullness, envIn}`. A new **native function** is legal at any
time — law B constrains **parameters**, not natives.* One clock even in the UI: the UI must integrate
the `v` it is handed rather than running its own rAF-time spin, or the spools drift off the audio;
wow/flutter visibly wobbles the tape line (sub-pixel amplified ×40); **input writes bright
magnetisation marks onto the tape that physically travel to each head and FLASH the head + emit a
decaying echo tick when read** — the echo pattern is literally watchable, Stop spools everything down
on screen, splice = a passing seam tick. Idle = dim, slow crawl, no marks; playing = bright marks +
flashing heads (obvious delta, law 9). Cost: ≤ 40 line segments + ≤ 32 mark sprites, zero filters.
Param reflection: Time (mark travel distance), Repeats (mark re-spawn brightness), Drive (mark
saturation→amber), Wow/Flutter (path wobble), Wear (dropout gaps in the tape line), Hiss (faint grain
overlay gated by env), Motor (spool-response lag), Bump/Width/Duck (meter ring on the output hub).

**B. `Echo Ruler`** — recycle the fb312 echo-timeline grammar from the DLY card: a time axis with tap
markers at {⅓,⅔,1}·T per Character, ghosts decaying per Repeats; wow wobbles marker x-positions;
tape-stop sweeps all markers rightward as pitch falls. Cheapest; weakest machine-feel.

**C. `Flux Loop`** — a circular tape loop (the RE-201 bin): the loop as a ring, write head at 12
o'clock, read heads as dots along the circumference, magnetisation marks orbit at `v`, splice = a
notch passing per revolution; self-osc leans the whole ring red as the limiter engages. Strong
identity, slightly costlier (ring gradient), best Stop drama (the ring physically stops).

Recommendation: **A** front and center with B's ghost-ruler embedded as a 12 px strip under it —
pattern readability + machine soul. All three react per law 9: idle=dim, note-on=bright.

---

## 6. Interplay — the device in the chain

- **Unity-through (default settings pass ≈ unity):** Drive 0 → S1 identity; Character masks 1/√N;
  per-Character `n_char` fixed makeup constants measured at build (harness gate §9-G6: dry-through at
  default ±0.5 dB, wet tap at Repeats 40% within ±1.5 dB of DLY's equivalent — the fb310 "perfect
  40%" reference). Never a program-dependent normaliser (§3.5).
- **Spectrum downstream:** every repeat loses top (gap-loss per pass) and gains bump-band LF — a Tape
  wash DARKENS a mix. Reverb after Tape = the classic (echo feeds the tank: RE-201 modes 5-11);
  Reverb *before* Tape re-echoes the tail into rhythmic mud (legal, sometimes wanted, never default).
- **Ordering wisdom:** `Distortion → Tape → Reverb` is the canonical dub chain (saturate the source,
  echo the saturation, wash the echoes). `Tape → Distortion` re-saturates every repeat — loud and
  flattening (the sat-after-mix eraser law: it erases the Mix balance — warn in manual). With the DLY
  device present, Tape ≠ Delay: run DLY as the clean rhythmic printer and Tape as the dirty wash, or
  either alone — but both at high feedback = two self-osc reservoirs that sum +6 dB into the limiter
  (§10-P9).
- **Dynamics:** Duck>0 makes Tape a call-and-response device (dry talks, wash answers). The in-loop
  limiter parks the circulating peak at **−12.9 dBFS** (§3.3, corrected). The wet output is the pattern
  mix taken *post* head-EQ, so its worst case is that park level plus the mix normalisation — state in
  code: **the Tape device NEVER emits above −6 dBFS wet**, which now carries ~7 dB of margin over the
  park level rather than the 2 dB the old −8 dBFS figure left.
- **The fb305/fb338 LANDMINE (read before wiring) — ✅ ALL LINE NUMBERS AUDIT-VERIFIED 2026-08-14:**
  a 4th send bus MUST join EVERY main-send exclusion sum or the dry leaks back at Mix 100. The three
  existing sums live at `PluginProcessor.cpp:7159`, `:7326`, `:7358` — **and each has an R twin one or
  two lines below (`:7161`, `:7328`, `:7360`), so it is six lines to edit, not three.** Grammar
  confirmed verbatim: `((rvbSendL?…) + (dlySendL?…) + (dstSendL?…)) * outputGain * kVoiceToFxPad`, with
  `constexpr float kVoiceToFxPad = 0.5f; // -6 dB` at `:6300` ✅. Add `tpeSend` to ALL SIX + the device's
  own exclusion block, exactly as fb338 did for Distortion. Miss one → "Mix 100 isn't fully wet" bug
  report from Max within the hour.
  ⚠️ *If you are working from memory's fb315 note citing "`:6979` / `:7111`" — those are **stale**
  (they were pre-fb338 line numbers; today `index.html:6979` is a robin SVG and `:7111` is a blank line
  in the ribbon row). The `PluginProcessor.cpp` numbers above are the ones that are true today, and
  they will drift again — **grep for `kVoiceToFxPad`, don't trust any line number in this file.***
- Latency: **zero reported** — rack law A. The device contains no lookahead anywhere; the only internal
  delay is the 2× oversampler's ~3.14 samples inside the S1 stage (§3.5), which lives *inside* the echo
  loop and must **never** be passed to `setLatencySamples()`. Reporting it would make the sample-aligned
  dry subtraction above phase-smear the leaked dry — the exact fb305 failure.

---

## 7. Presets — 14 factory sketches (values are knob %, T = Time)

| # | Name | Type/Character | Sketch |
|---|---|---|---|
| 1 | `First Machine` | Space/All Heads | T 1/4·, Rpt 45, Drv 25, Wow 20, Flut 25, Wear 15, Hiss 12, Bump 40, Wid 60, Duck 0 — the demo default |
| 2 | `Fifties Slap` | Deck/Slap 15ips | T 110 ms free, Rpt 8, Drv 35, Wow 8, Wid 30 — one clean rockabilly repeat |
| 3 | `King Dub` | Space/Heads 2+3 | T 1/4, Rpt 78, Drv 45, Bump 70, Duck 55, Hiss 20 — duck breathes with the riddim |
| 4 | `Runaway Riddim` | Space/Head 3 | Rpt **104**, Drv 55, Motor 70 — parked self-osc wash **≈+13 dB over program (−12.9 dBFS, §3.3 corrected)**, dies 12 s after note-off |
| 5 | `Swell Chamber` | Drum/Swell | T 1/8·, Rpt 60, Flut 15, Wear 0 — Echorec chime-wash, zero warble |
| 6 | `Clock Tower` | Drum/2+4 | T 1/8, Rpt 35, Wid 80 — geometric ping pattern |
| 7 | `ADT Wide` | Deck/Wide ADT | T 28 ms free, Rpt 0, Wow 18, Wid 100 — instant double-track |
| 8 | `Brown Boost` | Plex/Hot Input | T 1/8., Rpt 30, Drv 70 — EP-3 preamp push with one dark echo |
| 9 | `Cassette Memory` | Cassette/Ferric Worn | T 1/4, Rpt 40, Wow 45, Wear 55, Hiss 35 — the lo-fi bed |
| 10 | `Dying Walkman` | Cassette/Walkman | Wow 70, Wear 70, Motor 85, Hiss 45 — battery sag pitch dips |
| 11 | `Last Play` | Worn/Last Play | Rpt 50, Wear 80, Hiss 50 — the machine dies with the phrase |
| 12 | `Splice Storm` | Worn/Splice Storm | T 1/2, Rpt 65, Wear 90 — rhythmic seam thumps |
| 13 | `Brake Drop` | Reels/Brake Fast | Motor 60 — Stop pill = instant DJ spin-down of the whole mix tail |
| 14 | `Pump Quarter` | Reels/Pump ¼ | Rpt 55, synced ±30% v wobble — tempo-locked tape-warp pad motion |

Preset-level law (fb345 lesson): harness-check the 14 for level spread ≤ 6 dB at nominal program
before shipping — no Sludge/Gargle-class outliers.

---

## 8. CPU — budget + tiers

Per stereo instance @48 k, Apple-Silicon single core, estimates against measured in-tree relatives
(`DelayEngine` ≈ 0.5%, certified fb342 DST post-optimization ≈ 1.6% at Standard):
- Transport + 4 Hermite taps + head EQ (3 biquads/ch) + S1 + limiter + hiss + events ≈ **0.8-1.1%**.
  Single-head Types ≈ 0.6%.
  🛠️ **BUDGET CORRECTED — the "no oversampling" line was withdrawn in §3.5.** S1's floor is
  `ADAA-1 + 2×` on the memoryless stage only. Measured cost of that exact strategy in the DST bible
  (`:343-352`, stereo @48 k): **2× + ADAA-1 = 0.299%**. Our stage is one tanh, not a cascade, so budget
  **+0.25-0.30%** ⇒ **1.1-1.4% multi-head / ~0.9% single-head at Standard.** Still under the certified
  DST device's 1.6%, still Serum-class.
  Quality tiers: **Standard** `ADAA-1 + 2×` (the floor — never lower) · **High** `ADAA-2 + 2×` ·
  **Ultra** `ADAA-2 + 4×` (≈ +0.4% more) · **Eco** keeps `ADAA-1 + 2×` on S1 but drops Hermite→linear
  taps, halves event-machine density and slows the viz poll. **AA is not what Eco trims** — the DST
  bible's floor rule forbids it.
- Strategy: coefficients cached + recomputed on epochs only (weighted cache keys law fb342 ⑦);
  wow/flutter LFOs share the one clock (2 sin + 2 bp per sample total); noise via LCG; **control-head
  sleep** (fb342 ⑥): when env_in < −80 dBFS for 0.5 s AND the loop energy < −90 dBFS, skip events/
  hiss/viz and run taps-only decay; full wake on note-on (awake-head-sleep grammar from the DST
  device). Idle cost target < 0.15%.
- The 16.5 s buffer is memory, not CPU — allocated once in `prepare()`. 🛠️ **Corrected: 8.4 MB stereo
  float at 48 k** (`2²⁰` samples/ch after the pow-2 round-up), **16.8 MB at 96 k** (`2²¹`). The old
  "5.3 MB" understated it by 37% at 48 k and by 3× at 96 k.

---

## 9. Verify — perceptual harness gates (fb283: phase-independent metrics only)

Build `dst_cert`-style solo harness `tpe_cert.cpp` (clang++ -O2 -I shim -I Source), reusing
`rvb_perceptual.cpp` metrics. Gates:
- **G1 Types night-and-day:** all 7 Types at identical mid settings — pairwise discriminators of §2
  hit their stated numbers (tap maps, pitch-σ table, ρ_LR = −1 for Wide ADT, flux-variance ×5 for
  Worn, v(t) traces for Reels).
- **G2 Every knob 0→100:** monotone audible trajectory on its own metric (Wow→pitch-σ, Wear→HF-ratio
  + event count, Bump→LF magnitude @f_bump, Duck→wet RMS under program, Motor→measured glide τ). No
  plateaus (law 5). Use AM-probes for Duck (fb345 PK_AM law: static probes miss ducking).
- **G3 Silence class (the fb345 headliner):** every Type/Character × note-off → output < −80 dBFS
  within 15 s (12 s runaway release + decay). Hiss/scrape/thump gates verified with the SILENCE
  metric, not spectra.
- **G4 Loop stability:** Repeats 1.15 × 60 s torture at program +12 dB — bounded ≤ −6 dBFS wet,
  isfinite forever, **park-level ≈ −12.9 dBFS ± 2 dB** (§3.3 corrected; the old gate asserted −8 dBFS,
  which the exact fixed point would have FAILED by 4.7 dB — a gate that would have been "fixed" by
  detuning the limiter instead of the arithmetic).
- **G4b Degenerate transport:** hold `Stop` for 30 s, then `Reels/Rev Spool` for 30 s, at Repeats 1.15
  and Wear 100 — assert `isfinite` on every head-EQ coefficient, the splice phase, and the hiss sample.
  This is the gate for the §3.4 `ṽ` clamp, the §3.1 `v_floor`, the §3.7 phase accumulator and the §3.6
  `|v|` — four separate divide-by-zero / sign faults the audit found in the v→0 and v<0 paths.
- **G5 Click floors:** Time sweeps (both routings), Type/Character switches, Stop hammering —
  per-transition click metric under the honest per-char floors law (fb345).
- **G6 Unity-through** (§6) + **Mix 100 = fully wet** (dry residual < −60 dB) + preset level spread (§7).
- **G7 One-clock:** 10-minute drift test — wow phase, splice period, and tap pitch stay ratio-locked
  through a ±30% v automation (the stranded-clock proof).

---

## 10. Pitfalls — collected traps

- **P1 Zipper via the wrong Time routing:** gliding `delay` directly (DLY-style) on a v-routed Type
  kills the pitch signature; gliding `v` on Plex kills ITS signature. The routing switch is per-Type,
  §3.1 — test both in G5.
- **P2 Stranded clocks:** any LFO/event with its own phase accumulator will skew during v-glides
  (fb345 one-clock law). Everything reads the transport integrator.
- **P3 DC latch (Phase G silence class):** S1 asymmetry + feedback WILL latch DC without the in-loop
  22 Hz HP. Symptom: presets that go quiet-forever after one loud note.
- **P4 Limiter referenced to 0 dBFS:** never engages on the −26 dBFS bus → runaway becomes hard-clip.
  Knee at −16 dBFS (§3.3). The same class as the fb315 timidity root cause, inverted.
- **P5 Program-dependent makeup:** normalising S1 or the pattern mix by measured program flattens the
  very level-dependence the machine sells (Distortion Tape law). Fixed constants per Character.
- **P6 Noise-smoother ≠ walk** (Worn law fb345): smoothed white noise reads as vibrato, not wander.
  Use the certified walk.
- **P7 Free-running anything:** hiss beds, scrape, chaos v-wobble, self-osc — all env-gated or the
  device sings in an empty project (law 6; the #1 review-killer for tape plugins).
- **P8 Mono-sum collapse:** per-head pan spread (Width) must be symmetric and the Wide-ADT antiphase
  wobble is WET-only — check mono fold-down in G1 (Deck `Invert` is the only deliberate comb).
- **P9 Stacked reservoirs:** Tape + DLY both near self-osc sum into the master limiter — document, and
  keep the Tape wet ceiling at −6 dBFS (§6) so the sum stays legal.
- **P10 The fb305/fb338 exclusion sums** (§6): three exact lines + every future one. A 4th bus
  re-breaks fb305 unless `PluginProcessor.cpp:7159/:7326/:7358` all gain the new term.
- **P11 Reverse read (`Rev Spool`) + feedback:** writing while reading backwards through the write
  head = a glitch storm; mute the record path while v < 0 (§2.7 does).
- **P12 Buffer stale-state on SR change:** re-`prepare()` clears; the fb344 SIGBUS family.
- **P13 Character switch boot-quiet (P6 shared-slot trap fb345):** re-seat all per-Character gains at
  switch time, then crossfade outputs — never fade into un-seated state.

---

## 11. Hard-rule compliance checklist (laws 1-10, walked)

1. **Bus reality:** Drive = **30**·t^0.8 dB (house ANALOG D_max); limiter knee −15.9 dBFS; **park
   −12.9 dBFS**; hiss ceiling −30 dBFS; all stated relative to the measured −26 dBFS program.
   ✔ (§3.3/3.5/3.6 — all three numbers corrected by the 2026-08-14 audit)
2. **Chassis:** 2 dropdowns (Type **choice(7) at birth** × Character **choice(12) at birth**) + 8 back
   knobs 4×2 + front 3+Mix+2 pills; pragmatic Title-case names; **law C** (cardinality fixed at birth)
   stated and binding on §12 Q2/Q3. ✔ (§4)
3. **Time params:** synced 4 bars → 1/256 via the fb306 list. ✔ (§3.1)
4. **Mix 100 = fully wet** (G6); Type switch 75 ms dual-run crossfade, Character re-seat+fade —
   dropdowns never cut. ✔ (§3.9)
5. **0→100 evolve:** taper laws per knob, drum-floor Wow re-purpose, 100% = just-past-useful
   (Repeats 1.15 runaway, Wow 2.5% seasick, Hiss −30 dBFS). Types certified night-and-day by G1
   discriminators or cut (`Reels` pre-nominated — but see law C in §4: the *slot* ships either way).
   ✔ (§2/§9)
6. **Nothing free-runs:** self-osc 12 s env-release, hiss/scrape/thump/events env-gated; loop-gain
   audit with every stage named, max stable gain + **exactly-solved** park level stated. ✔ (§3.3)
7. **No clicks:** the §3.9 glide table covers every param + both Time routings + Stop — **plus the
   octave re-seat crossfade added in §3.1**, which the old draft's unbounded v-routing had no need for
   because it had no bound. ✔
8. **CPU:** **1.1-1.4%** budget at Standard including the `ADAA-1 + 2×` floor on S1 (the old "zero
   oversampling at every tier" verdict is withdrawn — §3.5), sleep mode, cache epochs. ✔ (§8)
9. **Audible↔visible:** Transport viz — every knob and both dropdowns have a named visual consequence;
   idle=dim/playing=bright stated. ✔ (§5.2)
10. **Recycle first:** §13 inventory — every reuse verified by file:line reads, none assumed. ✔

---

## 12. Open questions for Max

1. **Spring send?** The RE-201's 7 echo+reverb modes are half its identity. Ship pure-echo (chain to
   the Reverb device) or add a tiny fixed spring send inside `Space/Swell` only? (My rec: pure —
   `Swell`'s diffuser covers the wash, and the chain does it better.)
2. **Character cardinality:** 8 everywhere (matches DST) vs the authentic 12 for `Space`?
   12 is the marketing story ("the 12-mode selector"). ⚠️ **LAW C makes this urgent, not cosmetic
   (§4):** the option count can never change after the parameter is constructed. The audit's
   recommendation is therefore **build the param at 12 slots and use 8** — that keeps the door open at
   zero cost. Answering "8" is answering "never 12".
3. **`Reels` Type:** keep as Type 7, or cut to 6 and make `Brake Fast` the universal Stop while the
   pumps die? (The full arc: §2.7.) ⚠️ **LAW C again:** "cut to 6" is only free if it happens *before*
   `SYN_TPE_TYPE` exists. If there is any chance `Reels` returns, ship **choice(7) with `Reels`
   disabled** — you cannot re-grow the list later without invalidating every saved preset index.
4. **Second front pill:** `Stop` (chosen) vs `SOS` (freeze-loop) — SOS currently hides in Plex
   Characters. Which is the hero?
5. **Bus architecture:** does Tape join as the 4th SEND device (fb305-family wiring, §6 landmine) or
   as the first chain-insert-only device of the Multi-Device Chain epic? Affects nothing in this
   file's DSP, everything in wiring order.
6. **Time knob dual-routing** (v vs d) is invisible on the front panel — OK, or does Plex/Deck need a
   "slur" arc hint on the Time knob?
7. **RE-201 hardware numbers** (head ms at nominal, loop length): our 1:2:3 @ 3.1 s design is chosen,
   not copied — if you want spec-sheet authenticity for marketing copy, we should verify against a
   service manual before quoting anything.

---

## 13. Recycle inventory

🛠️ **Every line number below was re-read on 2026-08-14. Six were wrong in the original draft and are
corrected here** — the header used to claim "all verified by reading the code", which is exactly the
kind of claim law 10 exists to make people check.

| Reuse | From (✅ = re-verified this audit) | Into |
|---|---|---|
| Hermite fractional read (4-pt cubic) | ✅ `DelayEngine.h:230-249` (`readAt`, with the linear fallback under `!hq_`) / ✅ `TapeMachines.h:280` (`readCubic`) | all head taps |
| 20-entry sync-division list + host resolve | ✅ param `ParameterIDs.hpp:376` (⚠️ its inline comment is stale) — **the real list is `PluginProcessor.cpp:3456-3459`**, UI mirror `index.html:7485` | Time/Sync |
| M/S width (wet only) | 🛠️ ✅ `DelayEngine.h:208-211` (was cited as `:257-264`) | K7 Width |
| Input-ducking | 🛠️ ✅ `DelayEngine.h:213-220` (**was cited as `:218-224` — wrong**) | K8 Duck |
| In-loop HP→LP band edges | 🛠️ ✅ `DelayEngine.h:256-261` (`loopFilter`) | loop hygiene |
| `softClip` / `flush` denormal kill | 🛠️ ✅ `DelayEngine.h:315` / `:330` (neither was cited before) | §3.10 |
| 15 ms smoothing coefficient idiom | 🛠️ ✅ `DelayEngine.h:55` (`smCoef`) — **was cited as `:57`** | §3.9 glide table |
| LCG noise seed idiom | 🛠️ ✅ `DelayEngine.h:56` + member at `:360` — **was cited as `:46`** | §3.6 hiss |
| Buffer sizing + free-time clamp | ✅ `DelayEngine.h:40` (`ceil(16.5·fs)+8`, pow-2) / ✅ `:84` (1…16000 ms) | §3.1 |
| Triple-LFO cassette wow (0.6/2.2/7 Hz ±2.0/0.8/0.4 ms + drift) | 🛠️ ✅ **impl `TapeMachines.h:582-598`**, drift prepare `:514-516`, spec comment `:484` — **the old "`:483-516`" contains no LFO code** | `Cassette` §2.5 |
| SmoothRandom · SVFBandpass · AllpassDiffusionStage · DCBlocker | ✅ `TapeMachines.h:215 / 188 / 150 / 64` (all four confirmed) — note `AllpassDiffusionStage` is **one** stage; WireMachine chains **8** at `:811-818`, we chain 4 | walk targets, scrape bp, `Swell`, loop HP |
| **ADAA-1 wave shaper (both fallback branches)** | 🆕 ✅ `TerrainFilters.h:990-1030` (`struct WaveShaper`) — **missing from the original inventory, and §3.5 now depends on it** | S1 anti-aliasing |
| Compander | ✅ `MoogDelay.h:51` (`struct Compander`) | `Deck`/`Cassette` pump |
| TiltShelf grammar | ✅ `MoogDelay.h:108` (`struct TiltShelf`) | azimuth shelf |
| ⛔ pitch-in-feedback machinery — **do NOT import** | ✅ `MoogDelay.h:~306-398` (constant pitch offsets; our Doppler is free from the tap glide, §3.1) | — |
| 75 ms dual-run machine crossfade | ✅ `TapeProcessor.h:81-102` (`setMachine`) | Type switching |
| Head-bump/gap-loss fits + JA math (referenced, NOT imported) | ✅ `DISTORTION-BUILD-BIBLE.md:1435-1438`; JA cook + `Worn` at `DistortionEngine.h:1653-1700` | §3.4 / §0.2 boundary |
| Per-mode anti-aliasing floors (Soft Clip row governs S1) | 🆕 ✅ `DISTORTION-BUILD-BIBLE.md:439-461`; measured strategy table `:343-352` | §3.5 / §8 |
| Echo-timeline viz + rAF laws | fb312/fb313 DLY card | §5.2-B strip |
| FX viz-poll native grammar | 🆕 ✅ `PluginEditor.cpp:4862` (`getReverbBloom`) / `:4885` (`getDelayBloom`); `DelayEngine.h:226` (`getFeedbackViz`) | new `getTapeTransport`, §5.2 |
| Cassette/space/studio/wire viz prototypes | 🛠️ ✅ `Design/cassette-viz-preview.html:125-221` (canvas 2D; was cited as ":136-221 markup") + siblings | §5.2-A spool mechanics |
| Send-bus + exclusion wiring pattern | ✅ `PluginProcessor.cpp:6300` + **six** lines `:7159/:7161`, `:7326/:7328`, `:7358/:7360` | §6 landmine |
| Preset menu, pills, dropdowns, centerline chassis | fx-rack-v7 canonical + `.pmenu` | all UI |

---

## 14. Sources

Fetched and used this session:
- u-he Satin product page — https://u-he.com/products/satin/ (speeds, service panel, EQ standards, companders, delay mode)
- ChowTape User Manual (PDF) — https://chowdsp.com/manuals/ChowTapeManual.pdf (signal flow, all sections, solvers, loss params, TC-260 flutter)
- ChowTape repository — https://github.com/jatinchowdhury18/AnalogTapeModel (DAFx-19 paper: "Real-time Physical Modelling for Analog Tape Machines", J. Chowdhury; paper link http://dafx2019.bcu.ac.uk/papers/DAFx2019_paper_3.pdf)
- Boss RE-202 features — https://www.boss.info/us/products/re-202/features/ (3+1 heads, aged tape, saturation, runaway intensity)
- Arturia Delay TAPE-201 overview — https://www.arturia.com/products/software-effects/delay-tape201/overview (12 combos, L/R-PP-M/S, flutter/motor inertia/background noise, LFO 16 dest)
- AudioThing Outer Space — https://www.audiothing.net/effects/outer-space/ (12-mode structure 4/7/1, tape types incl. worn, per-head vol/pan)
- Strymon El Capistan support — https://www.strymon.net/products/elcapistan/ (controls + secondary functions, 20 s SOS)
- Strymon Deco — https://www.strymon.net/products/deco/ (two decks, lag −0.3→500 ms, wide stereo)
- Wikipedia: Roland RE-201 — https://en.wikipedia.org/wiki/Roland_RE-201 (head architecture)
- Wikipedia: Echoplex — https://en.wikipedia.org/wiki/Echoplex (moving record head, EP-3)
- Wikipedia: Wow and flutter measurement — https://en.wikipedia.org/wiki/Wow_and_flutter_measurement (4 Hz, scrape >100 Hz, 0.02%/0.08%, 3.15 kHz)
- UA Galaxy Tape Echo — https://www.uaudio.com/uad-plugins/delay-modulation/galaxy-tape-echo.html (control survey)

In-repo primary sources: `DISTORTION-BUILD-BIBLE.md` (JA math + cook constants + head-loss fits +
the per-mode anti-aliasing budget), `DelayEngine.h`, `TapeMachines.h`, `TapeProcessor.h`,
`TapeLoopProcessor.h`, `MoogDelay.h`, `TerrainFilters.h`, `PluginEditor.cpp` (viz natives),
`PluginProcessor.cpp` (bus + exclusion sums), Phase G certification memory (fb345 laws).
NOTE: WebSearch budget was exhausted (200/200) in both the research session and this audit — all web
research is direct-URL WebFetch of primary sources.

### 14.1 Audit verification log — 2026-08-14

**✅ RE-FETCHED AND CONFIRMED VERBATIM THIS AUDIT:**
- Wikipedia "Wow and flutter measurement" — 4 Hz wow/flutter split (**this corrected §1.7/§3.2**),
  scrape >100 Hz measured on a 10 kHz tone, 3.15 kHz test tone, 4 Hz most objectionable, 0.02% pro /
  0.08% cassette, and the 200 Hz weighting caveat *in full* (the source rejects the assumption).
- Wikipedia "Echoplex" — moving **record** head, EP-3 from **1970**, EP-3 is the sound-on-sound model,
  tape cartridge.
- Wikipedia "Roland RE-201" — 1 record + 3 playback heads, **1974**, free-running transport.
- Wikipedia "Binson Echorec" — magnetic drum, not tape (head count / Swell NOT confirmed; stub article).
- AudioThing Outer Space — **12 modes = 4 echo-only + 7 echo+reverb + 1 reverb-only**, 3 heads, three
  tape types incl. worn-out, per-head volume/pan, 2 spring tanks.
- Strymon Deco — Lag Deck **−0.3 ms → 500 ms** verbatim, Wide Stereo routing, 5-knob set.
- Strymon El Capistan — **20 second sound-on-sound looper** verbatim; all five secondary functions.

**⚠️ NOT VERIFIABLE THIS AUDIT — do not quote as fact:**
- **u-he Satin** (§1.6): u-he.com returned HTTP **429** twice. Every Satin figure is unconfirmed.
- **Binson Echorec** head count, "Swell" mode behaviour, production years.
- **RE-201** per-head millisecond figures, loop length, and the 3.1 s splice period (§3.7) — designed
  numbers (§12 Q7).
- **Echoplex EP-1 1959 date**; "warm, round, thick" EP-2 lore; the EVH "brown sound" attribution.
- **ChowTape** solver list (RK2/RK4/NR4/NR8/STN), signal-flow order, Sony TC-260 flutter capture, and
  the Loss/Degrade/Chew parameter lists (§1.6) — not re-fetched; the DAFx-19 numbers that *matter* are
  transcribed and validated in `DISTORTION-BUILD-BIBLE.md`, which WAS re-read.
- **Boss RE-202** 4th head / aged tape / Warp-Twist feature list; **Arturia TAPE-201** advanced panel;
  **UA Galaxy** control survey.

**🛠️ FACTUAL CORRECTIONS MADE BY THIS AUDIT** (each marked inline where it lands):
1. Wow/flutter split **6 Hz → 4 Hz** (§1.7, §3.2 comments).
2. Head-bump speed law **inverted → corrected**: `f ∝ v`, so 45 Hz @15 ips ⇒ **22 Hz** @7.5 ips, not
   90 Hz (§1.7, §2.1, §2.4, §3.4). *This defect is inherited from — and still present in —
   `DISTORTION-BUILD-BIBLE.md:1438`.*
3. Self-osc **park level −8 dBFS → −12.9 dBFS** (§3.3): the old figure used the `k/|x|` asymptote in a
   regime where it does not hold. Propagated to §6, §7 #4, §9-G4, §11.
4. §3.5 **misquoted** the Distortion bible's aliasing rule. Its Soft Clip row mandates `ADAA-1 + 2×` as
   a floor; "zero oversampling at every Quality tier" is withdrawn (§3.5, §8, §11).
5. Drive **D_max +36 → +30 dB** — the house ANALOG figure (`DISTORTION-BUILD-BIBLE.md:199-206`).
6. Buffer memory **5.3 MB → 8.4 MB** @48 k (16.8 MB @96 k) (§3.1, §8).
7. Cassette wow depth **~0.9% → ~2.4%** (§2.5) — it contradicted §3.2's own ceiling.
8. Wow taper: knob 25% is **0.27%**, not "~0.1%"; audible threshold at **~12%**, not 15% (§2.5, §3.2).
9. DLY-panel third FX page is **JUNE**, not EQ (§0.1).
10. Six recycle-inventory line numbers were wrong (§13), and three latent **divide-by-zero / sign
    faults** in the `v → 0` and `v < 0` paths were unguarded (§3.1, §3.4, §3.6, §3.7; gate G4b added).
