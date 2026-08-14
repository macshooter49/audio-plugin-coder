# Terrain Instrument — DELAY MOOG PORT PLAN
### The DLY panel (MoogDelay.h, MF-104S-inspired) → the FX Delay device, as new Types

**Status:** research complete, awaiting Max's scope call (§11).
**This is a PORT PLAN, not a from-scratch bible.** The primary source is the repo itself:
`Source/MoogDelay.h` (the engine Max already built, modeled on the Moogerfooger MF-104S
plugin) and `Source/DelayEngine.h` (the shipped, certified FX Delay device, fb296–fb313).
Web research seasons the port — it does not replace the code that already exists.

**Sibling documents:** `Design/DELAY-RESEARCH.md` (the fb296 multi-agent delay research —
Serum/Valhalla/Arturia/EchoBoy/u-he taxonomy; still authoritative for the existing 4 types),
`Design/DISTORTION-BUILD-BIBLE.md` (the per-family back-8 keying pattern this plan borrows),
`Design/REVERB-BUILD-BIBLE.md` (the Freeze-pill precedent).

---

## 0. Scope — what is being ported, what dies, what must not break

The Terrain front page has a **DLY panel** (button `index.html:5177`, panel markup
`index.html:5540-5652`, switcher `index.html:12175`) driving a **standalone MoogDelay
engine** (`Source/MoogDelay.h`, 635 lines) that lives inside the *tape-loop FX chain*
(`PluginProcessor.cpp:6715-6730` — between grain output and SpaceReverb, gated on `tapeOn`).
It is an MF-104S-flavored stereo BBD delay with five capabilities the shipped FX Delay
device does **not** have:

1. **True BBD companding** (compress-in / expand-out around the line) — `MoogDelay.h:51-105`.
2. **Clock-tracking bandwidth on BOTH sides of the line** (write-side pre-LP + read-side
   4-pole recon LP, both sliding with delay time) — `MoogDelay.h:493-504, 296-304, 435-440`.
3. **Pitch-shifted feedback** (−12/−7/+7/+12 per repeat) — `MoogDelay.h:306-396, 537-547`.
4. **A 7-waveform time-mod LFO** (Sine/Tri/Square/Ramp/Saw/S&H/Chaos) — `MoogDelay.h:512-527`.
5. **Freeze / infinite hold** (input fade-out + feedback override, 20 ms glide) —
   `MoogDelay.h:265-266, 401-403, 431`.

The FX Delay device (`Source/DelayEngine.h`, 361 lines; device grammar fb296–fb310) has
everything else and is certified: 4 Types (Digital/Tape/BBD/Diffuse), L/R Link + independent
times, the 20-division sync list (4 bar → 1/256), the ping crossfade, per-type feedback
makeup, the fb310 output-only Tone tilt, the fb312 echo-timeline visualizer.

**THE PLAN:** port capabilities 1–5 into `DelayEngine` as **two new Types + one rebuilt
Type** (§4), voiced through the Character dropdown, with zero new back-panel knobs. Then
retire the front-page DLY panel (Max: *"everything below the chopper gets replaced"* —
both options presented in §11). Nothing about the shipped Digital/Tape/Diffuse paths
changes byte-wise when the new types are not selected.

---

## 1. Repo recon — the complete inventory (line-anchored)

### 1.1 MoogDelay.h — engine anatomy

Signal flow per `MoogDelay.h:161-170` (comment block, verified against the code):

```
in → Compander_in → Pre_LP(time-tracked) → [write buffer]
[read buffer @ Hermite] → Recon_LP ×2 (4-pole, time-tracked) → Compander_out → TiltTone → wet
feedback = ReconLP'd tap → PitchShift → BBD_tanh_sat → Width cross-feed → back to delay input
dry ducker (peak-follower on dry) gates the wet; linear mix at the end
```

| Block | Lines | The exact math |
|---|---|---|
| `LPBiquad` | 9-48 | RBJ LP biquad, Q = 1/√2, clamped 20 Hz…0.45·fs. **⚠️ `setCutoff` computes sin/cos — and `processStereo` calls it 6× per sample** (recon ×4 + pre ×2 at :297-301, 435-437). ≈12 transcendentals/sample = the #1 CPU trap of this engine (§5.8). |
| `Compander` | 51-105 | Peak follower 10 ms atk / 100 ms rel. **Threshold −30 dBFS**; compress-in 4:1 *above* it, expand-out 4:1 *below* it (`overDb·0.25`, `underDb·4`). Per-sample `log10` + `pow` ×2 channels ×2 stages. **This exact structure is the fb308 "echoes died + hiss pumped" bug** — root-caused in §3.1. |
| `TiltShelf` | 108-159 | RBJ high-shelf @ 800 Hz, gain = tone·9 dB, tone ∈ −1..+1. Already re-derived in the device as `toneTilt` (`DelayEngine.h:263-271`, split @ 760 Hz, output-only per fb310). Nothing to port. |
| `Params` | 174-188 | 12 fields: `timeMs 5-1500 · feedback 0-1.10 · tone ±1 · character 0-1 · modDepth 0-1 · modRateHz 0-8 · modWave 0-6 · mix 0-1 · duck 0-1 · pitch enum 0-4 · width enum 0-2 · freezeHeld`. Chassis mapping table in §6.4. |
| Buffer | 195-198 | 2.0 s + 8 samples, plain `%` wrap (not mask). Device buffer is 16.5 s pow-2 mask-wrap (`DelayEngine.h:40-43`) — device wins. |
| Time glide | 205-207, 289-290 | 5 ms one-pole on delay-samples, per-sample → **repitch glide** (comb-click law honored; time sweeps bend pitch, the BBD-authentic behavior). |
| LFO | 273-284, 512-527 | Phase accumulator 0-8 Hz; 7 shapes: sine, tri, square, ramp-up, ramp-down, S&H (re-rolled on wrap), **chaos** = bounded random walk (`lfoChaos += 0.001·rand`, clamped ±1 — ⚠️ free-runs and never decays; §3.4). Mod offset = `lfo · depth · 0.30 · delaySamples` (:286) — **±30 % of the delay time**, vastly deeper than the device's ±6 ms chorus. |
| Recon law | 499-504 | `cutoff = 20000·(50/ms)` clamped **[800, 12000] Hz** → bright ≤ 83 ms, 800 Hz floor ≥ 1250 ms. THE clock-tracking BBD bandwidth law (matches hardware physics, §2.2). Pre-emphasis LP = `min(15000, recon·1.4)` (:493-497). |
| Pitch shift | 306-396 | Fractional read pointer inits at the tap, advances at `ratio` (0.5, 2^(∓7/12), 2.0 — :537-547); **re-syncs by jumping ±one delay period** when the pointer drifts past 0.5×/1.8× of the delay distance (:365-381); auto-bypasses below 40 ms with a 40-50 ms crossfade (:316-318, 391-395 — pitch-in-feedback at short times = ~200 Hz AM ring-mod garbage, documented in-code). Splice risk analyzed in §3.3. |
| BBD sat | 398-414, 506-510 | `tanh(x·d)/tanh(d)`, `d = 0.6 + character·1.8` (0.6→2.4). **Normalized tanh has small-signal gain d/tanh(d) ≥ 1** — the loop-gain table lives in §5.4. NaN guard + ±1.5 cap (:407-414). |
| Width | 416-428, 455-471 | enum: Mono collapse · Stereo M/S side ×1.6 · Ping = 90/10 cross-feed **in the feedback path** (true alternating ping). Device equivalent: `pingCur` crossfade `DelayEngine.h:191-201` — device wins (click-free morph). |
| Freeze | 265-266, 401-403, 431 | `freezeGain` glides 20 ms; input scaled ×(1−g); `effectiveFb = max(fb, g)` → buffer recirculates at unity while input fades. Clean, portable law. |
| Duck | 473-485 | Peak follower 5/200 ms on the **dry**; floor −40 dBFS, reduction = `−max(0, envDb+40)·0.45·duck` dB. Device has its own (`DelayEngine.h:214-220`, `1/(1+14a·env)`) — device wins; keep. |
| Interp | 549-598 | 4-pt Hermite (`readHermite` tap-relative + `readHermiteAbs` absolute for the pitch pointer). Identical polynomial to the device's HQ read (`DelayEngine.h:240-248`). |

### 1.2 The DLY front panel (UI to be retired)

`index.html:5540-5652`: 8 knobs — TIME · FB · TONE · CHAR · MOD (waveform popover, 7 shapes
:5609-5618) · RATE · MIX · DUCK — plus segment pills PITCH (OFF/−12/−7/+7/+12), WIDTH
(MONO/STEREO/PING), FREE/SYNC, ∞ HOLD, a section bypass, and `delayGridCanvas`.
Front-panel params `DLY_*` (`ParameterIDs.hpp:64-77`) are registered at
`PluginProcessor.cpp:1362-1455` (TIME 5-1500 ms skew 0.4 · FEEDBACK 0-1.10 · TONE ±1 ·
CHARACTER · MOD · MOD_RATE 0.05-8 Hz · MIX · DUCK · FREEZE · MOD_WAVE 7 · SYNC 2 ·
SYNC_DIV 9 entries 1/32…1/2 · PITCH 5 · WIDTH 3). Instrument-preset capture at :917-929 /
:1029-1041 / :9739-9752. Viz: `delayPalette` :16975, `drawDelayGrid` :17729 (echo cascade +
traveling comet, `windowSec = max(2, time·4)`, decay base `0.55 + fb·0.42`),
`drawDelayPanel` :18041. JS relays `dlyTime…dlyFreeze` :9310-9318.

### 1.3 The FX Delay device (the landing zone — fb296-310 grammar)

- **Engine:** `DelayEngine.h` — setters :82-98, `updateCoefficients` :102-136 (block-rate),
  `processSample` :139-223 (everything ramped per-sample via `smCoef` ≈ 15 ms).
  Types :177-184; per-type feedback makeup `fbMk = {Tape 1.0, BBD 1.00, Diffuse 2.15,
  Digital 1.28}` :198; loop soft-clip past ±1.4 :315-322; denormal flush :330;
  BBD time-tracking LP 5.2 kHz→2.3 kHz over 0-600 ms :121-126; wet-only M/S width :209-211;
  `getFeedbackViz` :226.
- **Processor block:** setup at `i==0` `PluginProcessor.cpp:7200-7282` (type pending-swap
  fade :7204-7210, sync resolve with the 20-division `divMult` table :7216-7240 —
  **4 bar → 1/256, the house time law**, free time `pow(8000, t)` = 1 ms→8 s :7244,
  feedback ×1.2 amplification :7263, equal-power mix :7278-7280). Insert lambda `applyDly`
  :7345-7374 (main-send with the fb305/fb338 exclusion sums :7358-7362); the 6-way
  `fxPerm_` serial switch :7383-7391.
- **Params:** `SYN_DLY_*` (`ParameterIDs.hpp:374-401`), layout `PluginProcessor.cpp:3448-3487`
  — TYPE choice(4) · CHARACTER choice(8) `{Clean,Warm,Vintage,Modern,Lo-Fi,Bright,Dark,Wide}`
  · SYNCDIV choice(20) ×2 (L/R) · 12 floats · 6 route bools · SYNC/LINK/PING/POWER/HQ bools.
- **UI card:** DEVS entry `index.html:7483-7485` (front knobs Time/Fdbk/Tone/Mix, pills
  Sync+Ping, back = Character + Sync dropdowns, 8 knobs Low Cut · Hi Cut · Spread · Width ·
  Mod Rate · Mod Depth · Time L · Time R); presets `DLY_PRESETS` :7737-7741; **echo-timeline
  viz** :8037-8090 (geometry from REAL time/fb/ping/link, pulse sweeps at the echo rate,
  brightness rides the 60 Hz `__fxBloomDly` C++ push :8083 — the fb312/fb342 laws);
  restore list :7919-7923; normalized choice writes :8236, :8270-8276, :8292 (**the
  cardinality-sensitive sites**, §3.5).

### 1.4 Side-by-side — what the port actually adds

| Capability | MoogDelay.h | DelayEngine.h today | Port verdict |
|---|---|---|---|
| Companding | full (broken thresholds) | **removed** in fb308 | **PORT — rebuilt** (§3.1, §5.3) |
| Clock-tracked bandwidth | write+read, 12k→800 | read-side only, 5.2k→2.3k | **PORT** full law into rebuilt type |
| Pitch feedback | ±5th/±oct | none | **PORT — new Type** |
| 7-wave time LFO, ±30 % depth | full | fixed sine ±6 ms + wow/flutter | **PORT — new Type** |
| Freeze | clean law | none | port the LAW, ship behind a pill only if Max wants it (§14 Q4) |
| Tilt tone | TiltShelf | `toneTilt` (fb310, output-only) | already ported — skip |
| Duck | −40 dB floor law | `1/(1+14a·env)` | device wins — skip |
| Width/ping | enum + 90/10 crossfeed | crossfade morph | device wins — skip |
| Hermite read | yes | yes (HQ) | identical — skip |
| Sync | 9 divisions | 20 divisions 4bar→1/256 | device wins — skip |

---

## 2. History and circuits — the lineage that defined the sound

### 2.1 The Moog line

- **MF-104 Moogerfooger Analog Delay (2000)** — 1,000 units, all-analog BBD, 40-800 ms,
  external feedback loop insert. Became the most sought-after Moogerfooger.
- **MF-104Z (2005)** ~1 s · **MF-104SD (2005)** 1.4 s, 250 units.
- **MF-104M (2012)** — vintage **MN3008** BBD chips, 800 ms max, **the 6-waveform LFO
  modulation section** (the defining addition), tap tempo, MIDI, spillover mode, dual
  outs. "The last of its kind." Small reissue batches whenever MN3008 stock surfaced;
  line discontinued Aug 2018.
- **Matriarch (2019)** — the same lineage on **MN3005** reissue chips: stereo analog delay,
  ≤700 ms, MIDI-syncable, stereo/ping-pong. DFAM/Grandmother carry mono cousins.
- **MF-104S plugin (2022, Moogerfooger Effects Plug-ins)** — the direct model for
  `MoogDelay.h`. Verified parameter set (moogconnect.net): TIME **SHORT 8-400 ms
  (brighter) / LONG 16-800 ms (darker)** — **the Range switch shifts everything in the
  loop by an octave** (clock doubling halves delay and doubles bandwidth — the origin of
  our Pitch-in-feedback idea); FEEDBACK self-oscillates above ~8/10; **LFO: 6 waveforms
  (Sine/Tri/Square/Saw/Ramp/S&H), 0.05-50 Hz, Amount onto delay time**; DRIVE (input
  tanh-ish saturation with a green/yellow/red LEVEL LED); OUTPUT + LINK (inverse-coupled);
  MIX; TIME SYNC + LFO SYNC; CV ins for TIME/FB/RATE/AMT/MIX; settings: TYPE Echo/Ping-Pong,
  TONE Legacy(dark)/Analog/Modern(full-band), TIMING Loose(clock drift)/Strict,
  LFO polarity, Spillover bypass, FEEDBACK MODE Legacy(darker)/Modern.
  `MoogDelay.h` implements: time+glide, feedback→1.10, drive-as-Character, 6+1 LFO waves
  (adds Chaos), tilt tone, mono/stereo/ping, duck, freeze, pitch feedback.

### 2.2 BBD physics — the numbers that voice the rebuilt type

From Raffel & Smith, *Practical Modeling of Bucket-Brigade Device Circuits*, DAFx-10
(the paper the whole industry cites):

- **Delay = N / (2·f_clk)** (N = stages; two clock phases per transfer). MN3005/SAD4096 =
  4096 stages, MN3008 = 2048, MN3007 = 1024. A 4096-stage echo at 300 ms needs
  f_clk = 6826⅔ Hz → the input must be band-limited to **3413 Hz**. MF-104M at 800 ms on
  2×MN3008 (4096 stages) → f_clk = 2560 Hz → **Nyquist 1280 Hz**. THIS is why long BBD
  repeats are dark — it is a hard sampling limit, not a tone knob.
- **Filters:** 3rd-order Sallen-Key anti-alias in + 3rd-order + 2nd-order "corner-correction"
  reconstruction out; cutoff chosen between **⅓ and ½ of f_clk**, as low as **1.5 kHz** in
  long echo circuits. → `MoogDelay.h`'s `20000·(50/ms)` law ≈ cutoff ≈ 1000/ms kHz ≈
  0.4·f_clk for a 4096-stage line. The law is physically right; keep it.
- **Compander:** NE570/571, compression ratio **2** (not 4), gain from a full-wave-rectified
  one-pole average, τ = 10000·C_rect with C = 0.22-1 µF → **τ = 2.2-10 ms**. Feedforward
  expander `f(x) = avg(|x|)·x`; feedback compressor `f(x) = x / avg(|f(x)|)`. **Reference-free
  — there is no threshold anywhere.** (§3.1: our −30 dBFS threshold version is the bug.)
- **Frequency-dependent insertion gain:** 0…+2 dB at LF falling to **−4…−6 dB at Nyquist**
  for any clock rate.
- **Noise:** SNR ≈ 60 dB, further suppressed by the compander → "reasonably treated as
  imperceptible" — supports fb308's NO-NOISE ruling; we do not add hiss.
- **Distortion:** THD ≈ **1.01^(N/1024) − 1** (≈1 % per 1024 stages), *level-independent*
  (charge-transfer loss, not clipping). Third-order fit: `f(x) = x − x²/8 − x³/18 + 1/8`
  (a = 1/8, b = 1/18, clamped 1−a−b above +1, −1−a+b below −1). Cheap, asymmetric, exactly
  the "grit" the tanh drive approximates.
- **Aliasing:** authentic BBD aliasing = a variable-rate resampling line (down-sample in at
  f_clk, up-sample out). We deliberately skip it (CPU; the darkness IS the audible signature,
  the aliasing mostly isn't at ≤12 kHz cutoffs) — same class of decision as the distortion
  bible's Tape carrier fold-in.

### 2.3 The rest of the family tree (what the greats expose)

- **EHX Deluxe Memory Man → Arturia Delay MEMORY-BRIGADE** (manual, dl.arturia.net):
  Input Level (preamp saturation), L/R Delay + Link, Sync, **BBD Size switch: "Deluxe"
  40-400 ms / "1100" 100-1000 ms** (chip count = range AND darkness), Stereo Offset,
  Stereo Width (M/S, widening only above center), Feedback (self-osc), **Chorus/Vibrato
  switch + Amount** (the DMM's baked LFO — modulation as identity), Echo Level, Delay Mode
  L/R / Ping-Pong / M/S, Blend (100 % = only the delay circuit — our Mix law), advanced:
  input EQ (LP 3k-20k auto-off at max, HP 20-1.2k, peak F/G/Q), unipolar env follower →
  any param, bipolar 6-shape LFO → any param.
- **u-he Colour Copy** (the BBD gold standard): 1 ms-1 s ×25-400 % rate scalar, **the clock
  is varispeed — time changes repitch, never crossfade** ("smooth with a capital SMOO"),
  5 morphable Colour macros (each = bandwidth + noise + sat tuple = our Character-as-tuple
  pattern), LFO 0.05-20 Hz onto rate/tap/amp with DYN env-scaling, ducking with FB-only
  target, Freeze.
- **ValhallaDelay:** Mode = character, Style = routing, mode-specific Age/Era — the
  orthogonality our device already adopted; the analog feedback path = filter + saturation
  *inside the loop*.
- **Serum 2** (prior research, DELAY-RESEARCH.md §4): delay = Feed, BPM sync, Link, per-side
  Time + scalar offset (DOT ×1.5 / TRIP ×1.333), one wet band-pass (Freq + inverted-Q), Mix,
  modes Normal/Ping-Pong/Tap→Delay, HQ interpolation new-and-default. **No echo visualizer**
  — a control panel plus the filter curve. Our fb312 echo timeline already exceeds every
  reference in this survey (§7.1).

---

## 3. ⚠️ The measured traps — why the naive port fails (read before building)

### 3.1 🔑 THE −30 dB COMPANDER TRAP (the fb308 root cause, now explained)

`Compander` (`MoogDelay.h:51-105`) compresses 4:1 **above −30 dBFS** on the way in and
expands 4:1 **below −30 dBFS** on the way out. On the FX bus the program sits at
**−26 dBFS** (measured — the distortion bible's headline; `kVoiceToFxPad = 0.5` at
`PluginProcessor.cpp:6300`). So program enters 4 dB above the threshold and the FIRST
repeat (post feedback ≈ −8 dB) lands at ≈ −34…−40 dBFS — **inside the expander's kill
zone, where every dB below −30 becomes 4 dB down**. Repeat 1 at −36 dB true → −54 dB
heard; repeat 2 is gone. That is *precisely* fb308's "the compander both SQUASHED the
feedback (so echoes died) and pumped an audible hiss" — the hiss pump is the same
expander riding its own 100 ms release across the noise floor.

**THE LAW: BBD companding on this bus must be REFERENCE-FREE.** Use the Raffel/NE570
structure — gain from a signal average, ratio 2, no threshold to mis-calibrate:

```
env(n)   = env(n−1) + k·(|x(n)| − env(n−1))      // full-wave avg, τ ≈ 5 ms  (k = 1−e^(−1/(fs·0.005)))
comp in  : y = x / max(√env_in,  ε)               // ratio-2 compressor  (feedback topology approximated feedforward)
expand out: y = x · max(√env_out, ε)              // ratio-2 expander
ε = 10^(−60/20)  — the gain clamp; below −60 dB the compander freezes (no noise pump)
```

Ratio-2 via √env keeps the loop's *level* invariant (compress→expand is identity for
steady state) while producing the two audible signatures: **attack overshoot bloom**
(env lags transients → first ms passes hot) and **release breathing** on decaying tails.
No `log10`/`pow` per sample (the in-tree version burns 4 transcendentals/sample), no
threshold, no bus dependency. Feedback survives because the round trip is gain-neutral.
**Verify gate:** loop-gain sweep with compander on vs off must match within ±0.5 dB at
fb = 40 % (echo count identical); the pump must show as ≥ 3 dB of gain modulation on a
−26 dBFS 4 Hz AM probe (audible breath) — the honest AM probe per the fb345 probe-craft.

### 3.2 🔑 Small-signal loop gain of the normalized tanh (the "mosquito buzz" math)

`bbdSaturate(x,d) = tanh(x·d)/tanh(d)` has derivative at 0 of **g₀ = d/tanh(d) ≥ 1**:

| Character drive d | g₀ = d/tanh(d) | fb knob where loop gain = 1 (fb×1.2 law, :7263) |
|---|---|---|
| 0.6 (char 0) | 1.117 | 0.746 → knob ≈ 62 % |
| 1.2 | 1.437 | 0.580 → knob ≈ 48 % |
| 1.8 | 1.903 | 0.438 → knob ≈ 37 % |
| 2.4 (char 1) | 2.437 | 0.342 → knob ≈ 29 % |

The in-code comment (`MoogDelay.h:398-400`, "at char=0, gain ~0.85x") is a large-signal
estimate; the SMALL-signal gain is what governs the tail of a decaying repeat chain, and
it exceeds 1 at every drive. The old MF port "mosquito buzz" was this: tails decay into
the linear region where the loop quietly has more gain than the knob says.
**THE LAW (loop-gain law, fb306-310):** the rebuilt type's feedback makeup must cancel
g₀ exactly: `fbMk_bucket(char) = tanh(d)/d` for the selected Character's d — then the
fb knob means the same thing at every drive, unity loop lands at knob ≈ 83 %
(1/1.2), and self-oscillation begins just above — matching the certified Tape behavior
(`DelayEngine.h:193-199`). State every stage: loop gain = `fb·1.2 · fbMk · g₀_sat ·
H_hicut(f) · H_lowcut(f) · H_recon(f) · comp_roundtrip(=1)`. With fbMk = tanh(d)/d the
product at the loop's dominant frequency is `fb·1.2 · H_filters ≤ 1.2·H_filters` —
bounded, and the ±1.4 softClip (`DelayEngine.h:315-322`) makes the >1 regime musical
self-osc, exactly like Tape. **Max stable loop gain: 1.2 (knob 100 %), softClip-bounded;
BIBO by the same argument as fb308.**

### 3.3 The pitch-feedback re-sync splice

`MoogDelay.h:365-381` re-syncs the drifting pitch pointer by teleporting it ±one delay
period. Landing "one echo earlier" masks the splice most of the time, but it is still a
hard discontinuity in the feedback path → a click per re-sync (period ≈ delayTime/|ratio−1|;
at +12/500 ms that is every 500 ms). **Port upgrade (mandatory):** dual-tap the pitch
read — 10 ms equal-power crossfade from the old pointer to the jumped pointer (two extra
Hermite reads only during the fade). Same class of fix as the fb345 char-switch deferred
fade. The <40 ms auto-bypass with its 40-50 ms crossfade (:316-318, 391-395) ports as-is —
it is correct and documented (pitch-in-feedback at slap times = ring-mod trash).

### 3.4 The Chaos LFO free-runs (and integrates) — two Phase-G laws apply

`lfoChaos += 0.001·rand` (:279-281) is an undamped random walk: it free-runs while the
device idles and its DC parks the delay time off-center indefinitely. Per the fb345
**Worn-walk law** (a per-sample noise smoother ≠ a walk; walks need leak) give it a decay:
`chaos = chaos·(1 − 1/(fs·τ)) + 0.001·rand`, τ ≈ 2 s. And per the **one-clock law**
(fb345 DIGITAL: phase accumulators integrate glide skew) the new Drift type runs **ONE
phase accumulator**, R read at +90°, so L/R never walk apart during rate glides — the
device's chorus already does this (`DelayEngine.h:157-158`); the port must keep it.

### 3.5 🔑 The choice-cardinality trap (fb342 session law ①)

Adding Types changes `SYN_DLY_TYPE` from choice(4) → choice(6). The UI writes choices
NORMALIZED (`setSynParam(p, i/(N−1))` — sites `index.html:8236, 8270-8276, 8292`, preset
apply :7865-7869, restore :7919-7923), so every N-dependent write site must move in
lockstep or old indices land on the wrong type. Rules: **append new types at the END**
(old APVTS state stores raw index values — appended entries leave 0-3 untouched);
`Character` stays **exactly choice(8)** re-labeled per type (zero cardinality change);
bump `TERRAIN_BUILD`; verify by loading a pre-port session with each of the 4 legacy
types selected (the fb341 legacy-anchor pattern at :3488-3495 is the precedent).

---

## 4. §Types — the proposed Type dropdown (6 entries, append-only)

Type list after the port: **Digital · Tape · Bucket · Diffuse · Drift · Shift**
(indices 0-3 unchanged; "Bucket" is the re-voiced index 2 "BBD"; 4-5 appended).
Each type = a different *degradation mechanism* (the certified device law), and every
new mechanism is code that already exists in `MoogDelay.h` — this is a port, not an
invention. Six is the ceiling: a 7th ("Pedal" — drive-forward MF voice) failed the
night-and-day test against Bucket and was folded into Bucket's Character column.

### 4.0 Kept types (byte-preserved)

- **Digital (0)** — clean control group. Unchanged (`DelayEngine.h:183`).
- **Tape (1)** — asym tanh + 7.2 kHz head-gap LP + wow/flutter (:274-280). Unchanged.
  ⚠️ Known debt (flagged, not this port): `x + 0.06x²` in a feedback loop is a DC pump —
  the fb345 AC-coupled-feedback law says the loop should see a DC block when Low Cut = 0.
  Fix alongside the Bucket build (one shared one-pole HP @ 5 Hz in-loop, both types).
- **Diffuse (3)** — 4-allpass smear (:296-313). Unchanged.

### 4.1 BUCKET (index 2, rebuilt) — the full MF-104/BBD physics port

*Lineage: MF-104M · MN3005/3008 · NE570 · Raffel & Smith DAFx-10 · Colour Copy.*

**Recipe** (replaces `bbd()` at `DelayEngine.h:287-293`; per-channel state ~6 floats):

```
write side: x → compand_in (§3.1) → preLP one-pole @ min(15k, recon·1.4) → buffer
read  side: tap → reconLP ×2 (two one-poles, 12 dB/oct) @ recon(char, timeMs)
            → BBD grit: y = x − x²/8 − x³/18 + 1/8, then drive tanh(y·d)/tanh(d)
            → compand_out → (this is fL/fR into the shared feedback write)
recon(char, ms) = clamp( K_char · (50/ms) · 20000 , floor_char , ceil_char )   // §1.1 law
fbMk = tanh(d)/d                                                               // §3.2 law
```

Coefficients update at block rate in `updateCoefficients()` (:102-136) — **never
per-sample** (§5.8); the cutoff target is smoothed per-sample like every other coeff.
Grit before drive (poly then tanh) keeps the level-independent transfer loss under the
level-dependent drive — matching the paper's observation that BBD THD is not clipping.

**Character column (8, re-labeled; each row = {K/floor/ceil bandwidth law, drive d,
compander τ_in/τ_out, baked mod}):**

| # | Name | Bandwidth (recon) | d | Compander | Baked mod | The voice |
|---|---|---|---|---|---|---|
| 0 | Short Clock | ×2 K, floor 1.6k, ceil 12k | 0.9 | 3 ms | — | MF-104 SHORT range: bright slap |
| 1 | Long Clock | ×1 K, floor 800, ceil 12k | 1.2 | 5 ms | — | **the reference — MF-104 LONG** |
| 2 | Hot Bucket | ×1, floor 800 | 2.4 | 3 ms hot | — | drive-forward MF (old char = 1) |
| 3 | Memory | fixed 4.5k (no tracking < 400 ms) | 1.0 | 6 ms | chorus 0.5 Hz ±0.4 ms | the DMM chorus voice |
| 4 | Matriarch | ×1.4, floor 1.2k | 1.1 | 4 ms | — | the 700 ms stereo pedigree, brighter floor |
| 5 | Club 1100 | ×0.7, floor 600 | 1.4 | 8 ms slow | — | long-chip murk (Arturia "1100") |
| 6 | Worn Cart | fixed 3.5k | 1.6 | 5 ms | wow 0.4 Hz ±0.8 ms | cassette-adjacent grunge |
| 7 | Clean Clock | ×3 K, ceil 14k, floor 2k | 0.7 | off | — | modern/hi-fi BBD, compander bypass |

**Night-and-day discriminator (the measurable tell):** per-repeat HF-ratio slope. Probe:
−26 dBFS pink burst, fb 50 %, Time 600 ms → the centroid of repeat n must fall along the
recon law (Long Clock: repeat 3 centroid < 1.2 kHz) while Digital's stays flat and Tape's
falls only to the fixed 7.2 kHz shelf. Plus the compander breath: ≥ 3 dB gain modulation
on the AM probe (§3.1) — a metric NO other type shows. Character sweep gate: Short vs
Club 1100 centroid ratio ≥ 2.5× at identical knobs (chars must be night-and-day or die —
the current 8 near-dead labels are exactly what fb325 bans).

### 4.2 DRIFT (index 4, NEW) — the Moogerfooger LFO section as a Type

*Lineage: MF-104M's 6-waveform mod section — the thing that made the 2012 reissue famous.*

**Recipe:** the Digital read path (clean, full-band) with the read position modulated by
the ported 7-shape LFO (`MoogDelay.h:512-527`), depth law `offset = lfo · depth · 0.30 ·
delaySamples` (:286) — **±30 % OF THE DELAY TIME**, not ±6 ms. At 500 ms / depth 100 %
the tap swings ±150 ms → pitch excursion ratio 1±0.30·2π·rate·T… i.e. audible instant
vibrato→siren→shatter. One phase accumulator, R at +90° (§3.4). Waveform = **Character**:

| # | Character | Behavior at the read head | The tell |
|---|---|---|---|
| 0 | Sine | classic pitch vibrato → seasick | smooth F0 wobble |
| 1 | Triangle | linear up/down glide | constant-slope pitch ramps |
| 2 | Square | **two alternating tap positions** — echo teleports | hard flam/glitch pairs, zero glide |
| 3 | Ramp Up | slow reverse-doppler rise, snap back | rising chirp per cycle |
| 4 | Ramp Down | tape-stop fall, snap back | falling chirp per cycle |
| 5 | Sample & Hold | new random tap each cycle | quantized random echo grid |
| 6 | Chaos | leaky random walk (§3.4) | drunken tape, never repeats |
| 7 | Pump | env-follower on the INPUT drives the offset | playing-intensity bends time (input-gated — the fb325 event-machine pattern) |

Square/S&H **glide between targets ~4 ms** (comb-click law — a snapped delay pointer is
the textbook click); the glide is short enough to read as a jump-cut, long enough to be
click-free (verify: residual click < −60 dB on silence-gap probe, the fb345 honest
per-char click floor).

**Discriminator:** F0-deviation depth in cents on a 220 Hz probe through the wet path —
Drift @ depth 50 % ≥ ±700 cents where Tape's Wow maxes near ±35 cents. An order of
magnitude apart = night-and-day vs Tape by construction. Mod Rate/Depth become THE hero
controls for this type (both already on the back-8 — zero new params).

### 4.3 SHIFT (index 5, NEW) — pitch-shifted feedback

*Lineage: the MF-104S Range-switch octave jump, MoogDelay's pitch enum, the AMS DMX/
Bode-shifter dub tradition; Valhalla Pitch mode is the modern reference.*

**Recipe:** port `MoogDelay.h:306-396` onto the device buffer — a second fractional
pointer advancing at `ratio` per sample, re-synced one-delay-period back with the §3.3
dual-tap crossfade, feeding the feedback write ONLY (the first echo is unshifted; each
subsequent repeat transposes again — the cascade IS the sound). Auto-bypass < 40 ms with
the 40-50 ms blend (:316-318). Interval = **Character**:

| # | Character | Ratio per repeat | The voice |
|---|---|---|---|
| 0 | Octave Down | 0.5 | sub-madness, darkness cascade |
| 1 | Fifth Down | 2^(−7/12) | melancholy spiral |
| 2 | Fourth Down | 2^(−5/12) | gentler descent |
| 3 | Fourth Up | 2^(+5/12) | rising suspension |
| 4 | Fifth Up | 2^(+7/12) | the dub-siren ladder |
| 5 | Shimmer | 2.0 | the octave-up shimmer delay |
| 6 | See-Saw | alternate 2.0 / 0.5 per repeat | octave ping-pong in pitch |
| 7 | Haze | 2.0 ± 15 cents, L/R detuned ∓ | detuned shimmer wash |

See-Saw flips the ratio each time the pulse crosses the write head (one comparison);
Haze offsets the two channels' ratios — free width. **Feedback stability:** the pitch
pointer re-reads material that already passed the loop filters, so the loop gain analysis
of §3.2 holds unchanged; upward shifts push energy toward Hi Cut and self-limit
(the classic reason shimmer delays stay stable); downward shifts pile LF — Low Cut ≥
80 Hz is enforced as a type-entry default nudge (never a hard floor — knob stays live).

**Discriminator:** per-repeat F0 ladder — 220 Hz in, fb 60 %: repeat n must measure at
220·ratioⁿ ±10 cents (n ≤ 4). No other type moves F0 *between* repeats.

### 4.4 The cut list (and where the sound went)

- **"Pedal" drive type** — cut; drive lives in Bucket Characters 2/6 (§4.1).
- **Freeze as a Type** — never a type; it is a state law (§1.1) parked for the Hold pill
  decision (§14 Q4).
- **MF LFO-onto-amplitude / Colour-Copy tap-mod targets** — cut (a delay is not a mod
  matrix; Terrain's Patcher endgame owns that).
- **Hardware clock noise / hiss** — cut on the fb308 NO-NOISE ruling + Raffel's
  "imperceptible after companding" measurement.
- **BBD variable-rate aliasing emulation** — cut (§2.2), CPU not worth an artifact that
  the recon LP mostly hides.

---

## 5. §DSP core — algorithms, param laws, stability, oversampling verdict

### 5.1 Where the port lands (the exact seams)

1. `DelayEngine::setType` clamp 0-3 → 0-5 (`DelayEngine.h:82`).
2. The per-type character switch (:177-184) gains `case 4:` (Drift = clean read; the mod
   is applied at the read-offset stage, see 5.2) and `case 5:` (Shift = clean read; the
   pitch tap replaces `fL/fR` in the feedback term only, :200-201).
3. `case 2:` (BBD → Bucket) replaced by §4.1's chain; per-Character tuple table is a
   `static constexpr` struct array — no allocations, no branches beyond the existing switch.
4. `updateCoefficients()` (:102-136) computes: recon target from the Character tuple +
   `timeMs_`, compander k's, drive d + `fbMk_bucket = tanh(d)/d` folded into the existing
   `fbMk` expression (:198), Drift LFO increment, Shift ratio.
5. `processSample()` — the mod-offset line (:159-165) gains the Drift branch:
   `type==4 ? lfoShape(chr, ph) · modDepthCur · 0.30 · delCurL : (existing chorus law)`.
   Everything else (filters, ping, width, duck, mix) is untouched.

### 5.2 Param laws (range · taper · glide — every knob, no dead zones)

| Param | Law (unchanged unless noted) | Glide |
|---|---|---|
| Time | sync: 20-division table `PluginProcessor.cpp:7216-7240` (4 bar→1/256); free: `pow(8000,t)` ms (:7244) | per-sample one-pole `smCoef` ≈ 15 ms → repitch glide (`DelayEngine.h:142-143`) — BBD-authentic; keep |
| Feedback | `knob·1.2` (:7263) → engine clamp 1.15 (:87) → in-loop cap 0.98 + softClip | 15 ms ramp |
| Tone | output tilt ±85 % @ 760 Hz (:263-271) — NOT in loop (fb310) | 15 ms |
| Low/Hi Cut | `20·50^t` Hz / `1200·15^t` Hz (:7265-7266), in-loop one-poles | block + smoothed state |
| Spread | R = L·(1+0.35·s) linked (:107-111) | 15 ms |
| Width | wet-only M/S 0-1.6 (:209-211) | 15 ms |
| Mod Rate | 0.05-8 Hz (:7271). **Drift: re-taper to `0.05·(160)^t` Hz = 0.05-8 exp** — the linear taper wastes the bottom half where vibrato lives (fb325 taper law) | phase-continuous (one clock) |
| Mod Depth | 0-1; Drift depth is ±30 % of Time (§4.2); others keep ±6 ms chorus | 15 ms |
| Time L/R | fb306 link grammar unchanged | 15 ms |
| Character | choice(8) — **fade-swap on change**: reuse the type-pending fade (:7204-7210) at Character granularity for Bucket/Shift (a drive or ratio jump is audible); Drift waves may hot-swap (phase-continuous) | 30 ms fade |

### 5.3 The compander (final form) — §3.1's law, per channel, Bucket only

State: 2 envelopes/channel. Cost: 4 mult-adds + 2 sqrt + 1 div per sample per channel
(sqrt via `std::sqrt` — SIMD-friendly; NO log/pow). Round-trip gain identity verified by
construction; the freeze/idle guard is the ε-clamp (gain frozen below −60 dB → no pump
on silence → the fb345 SILENCE-metric gate passes: idle output < −90 dBFS).

### 5.4 Stability (the loop-gain ledger — law 6)

Every gain stage inside the recirculating path, per type:

```
Digital : fb·1.2 · 1.28(fbMk) · H_lc·H_hc                          ≤ 1.2·1.28·1 = 1.54 → softClip bounds
Tape    : fb·1.2 · 1.0 · g_tanh(≈1.35 @ d1.4) · 0.86 · H_7k2·H_lc·H_hc
Bucket  : fb·1.2 · tanh(d)/d · d/tanh(d) · H_recon·H_lc·H_hc · 1(compander) = fb·1.2·H  ✓ exact
Diffuse : fb·1.2 · 2.15 · allpass(unitary) · H_lc·H_hc
Drift   : = Digital (mod moves the tap, adds no gain)
Shift   : = Digital path gains; energy migrates spectrally per §4.3
```

Max stable loop gain **1.2** (knob 100 %) everywhere, softClip past ±1.4 converts the
>1 regime into bounded self-oscillation (the certified fb308 BIBO argument). Freeze (if
shipped) pins loop gain to exactly 1.0 with input faded — bounded by construction.

### 5.5 Oversampling verdict

**None. Nowhere.** The nonlinearities in the loop (tanh ≤ d 2.4, the cubic grit, softClip)
run at drive levels where NPR-style aliasing sits below the recon/hi-cut filtering that
immediately follows them *inside the loop* — every pass re-lowpasses the previous pass's
aliases. The distortion bible's measured master rule (≥ 2× only above measured audibility)
was checked against this topology: an in-loop LP at ≤ 12 kHz after a tanh at d ≤ 2.4 keeps
folded energy < −60 dB below carrier at −26 dBFS program. HQ stays what it is: the cubic
Hermite read toggle (`DelayEngine.h:230-248`). This is the CPU law working for us — a
delay is the one FX family where oversampling buys nothing audible.

### 5.6 Denormals / DC / NaN

- `flush()` on every buffer write (:204-205, 330) — extend to the compander envelopes and
  the Shift pointer's crossfade accumulators.
- DC: one shared in-loop DC block (one-pole HP @ 5 Hz) engaged whenever Low Cut < 30 Hz
  for Tape (asym x² pump, §4.0) and Bucket (grit is asymmetric: +1/8 offset term) — the
  fb345 AC-coupled-feedback law. Costs 1 mult-add; silent at any Low Cut > 30 Hz.
- The `isfinite` guard + cap from `MoogDelay.h:407-414` ports into the Shift pointer math.

### 5.7 The Shift pointer (final form)

```
if (!init) pos = wr − delCur; init = true
out = hermite(buf, pos);  pos += ratio
d = distBehindWrite(pos)
if (ratio > 1 && d < 0.5·delCur) → target = pos − delCur, xfade 10 ms (two-tap)
if (ratio < 1 && d > 1.8·delCur) → target = pos + delCur, xfade 10 ms
pitchActive = clamp((currentMs − 40)/10, 0, 1); out = lerp(tap_unshifted, out, pitchActive)
```

See-Saw flips `ratio ↔ 1/ratio` at each re-sync. Haze: `ratioR = ratio·2^(∓15/1200)`.

### 5.8 The per-sample-coefficients fix (port improvement, measured class)

`MoogDelay.h` recomputes six biquads per sample (§1.1) — ~12 sin/cos per sample ≈ the
cost of the entire DelayEngine. The port keeps the DEVICE pattern: coefficients at block
rate in `updateCoefficients()`, cutoff-state smoothed per-sample by the existing one-pole
lattice. The recon law is a function of `timeMs_` which only changes at block rate anyway.

---

## 6. §Chassis map — 11 params, locked geometry, zero new knobs

The delay device's chassis is SHIPPED and frozen (fb275 spec; DEVS `index.html:7483-7485`).
The port adds **no** params — it re-keys meaning per type, the distortion device's
proven pattern (its back-8 keys to FAMILY; ours keys Character + Mod per TYPE).

### 6.1 Front card (3 + Mix, pills, viz)

| Control | Name | Notes |
|---|---|---|
| Knob 1 | **Time** | sync-div label when Sync pill on (`dlyTimeLabel` :7686) — unchanged |
| Knob 2 | **Fdbk** | §3.2 makeup makes it honest across all 6 types |
| Knob 3 | **Tone** | output tilt — unchanged |
| Knob 4 | **Mix** | equal-power, 100 % = fully wet (:7278-7280) — unchanged |
| Pill 1 | **Sync** | unchanged |
| Pill 2 | **Ping** | unchanged |
| Viz | echo timeline | + the §7 per-type layers |

### 6.2 Back panel — 2 dropdowns + 4×2 knobs

- **Dropdown 1 — Type:** `Digital · Tape · Bucket · Diffuse · Drift · Shift`
  (append-only; §3.5). Pragmatic names: *Bucket* says the mechanism without a trademark;
  *Drift* says what it does to time; *Shift* says what it does to pitch.
- **Dropdown 2 — Sync:** the 20-division list — untouched (house time law).
- **Character (dropdown 1 of the pair on the back, existing `d1`):** stays choice(8);
  the OPTION LABELS swap per type (§4 tables) exactly like the UI already swaps preset
  lists per type (:7865). Cardinality never changes.

**Back-8 (names unchanged — every knob live for every type, the fb308 precedent):**

| Slot | Knob | Digital/Tape/Diffuse | Bucket | Drift | Shift |
|---|---|---|---|---|---|
| 1 | Low Cut | in-loop HP | in-loop HP (+DC law §5.6) | in-loop HP | in-loop HP (default nudged 80 Hz on down-shift chars) |
| 2 | Hi Cut | in-loop LP | in-loop LP *under* recon | in-loop LP | in-loop LP (shimmer self-limit) |
| 3 | Spread | L/R offset | L/R offset | L/R offset | L/R offset |
| 4 | Width | wet M/S | wet M/S | wet M/S | wet M/S (Haze pre-widened) |
| 5 | Mod Rate | chorus rate | chorus rate | **LFO clock (hero)** | shimmer-detune wobble rate |
| 6 | Mod Depth | chorus depth | chorus depth | **±30 % Time (hero)** | detune depth ±25 cents |
| 7 | Time L | fb306 link | fb306 link | fb306 link | fb306 link |
| 8 | Time R | fb306 link | fb306 link | fb306 link | fb306 link |

### 6.3 What happened to every MoogDelay::Params field (the 12-param audit)

| MoogDelay param | Fate in the device |
|---|---|
| timeMs | → SYN_DLY_TIME (better law: 1 ms-8 s + 20 divisions) |
| feedback | → SYN_DLY_FEEDBACK (×1.2 + honest makeup) |
| tone | → SYN_DLY_TONE (fb310 output tilt — already better) |
| character (drive float) | → **Bucket Character tuple** (drive is voiced, not a knob) |
| modDepth / modRateHz | → SYN_DLY_MODDEPTH/MODRATE (Drift re-tapers; §5.2) |
| modWave (7) | → **Drift Character** (8 = 7 + Pump) |
| mix | → SYN_DLY_MIX (equal-power — better) |
| duck | → SYN_DLY_DUCK exists already (default 0; Max dislikes ducking — stays hidden) |
| pitch (enum 5) | → **Shift Character** (8 intervals) |
| width (enum 3) | → SYN_DLY_WIDTH + PING pill (already richer) |
| freezeHeld | → parked; the law is documented (§1.1) for the Hold-pill decision (§14 Q4) |
| (sync/div, front DLY_SYNC_DIV) | → SYN_DLY_SYNC + 20-division SYNCDIV (superset) |

Nothing from the Moog engine is lost except hardware-noise emulation (deliberate, §4.4).

---

## 7. §Visualizers — survey, then the three layers for OUR card

### 7.1 How the greats show a delay (surveyed mechanisms)

- **Moog MF-104S plugin:** a photoreal pedal — animated knobs, the drive LEVEL LED
  (off/green/yellow/red). No timeline; the LED is the only live element.
- **Arturia MEMORY-BRIGADE:** pedal skin + Advanced panel (env-follower level LEDs).
  No echo geometry anywhere.
- **u-he Colour Copy:** knobs + small meters; colour macro morphs are knob-lit only.
- **ValhallaDelay:** flat text-styled panel, zero visualization.
- **Serum 2:** delay panel = controls + the wet band-pass curve; no tap display.
- **Verdict:** the field is BAD at this. Our certified fb312 **echo timeline**
  (`index.html:8037-8090` — tap x = real time, heights = real feedback decay, pulse
  sweeping at the true echo rate, Ping alternation on split lanes, brightness riding the
  60 Hz `__fxBloomDly` push) already leads. The port ADDS three cheap per-type layers
  to it rather than inventing a new surface (recycle law).

### 7.2 Layer 1 — Mod ghost-swing (Drift; also chorus at low depth)

Each tap's `<line>` gains a horizontal oscillation about its grid slot:
`dx = tapSpacing · 0.30 · depth · lfoShape(chr, pulsePhase·k)` — THE actual ±30 % law,
the actual selected waveform, the actual rate (rate/echo-rate ratio drives k). Square
teleports taps between two x's; S&H re-rolls per period; Chaos wanders; Pump kicks with
the live bloom value. Implementation: update `transform=translate(dx,0)` on the existing
`.dtap` groups inside the existing rAF tick — ~14 ops/frame, no new nodes, no shadowBlur
(fb342 per-frame-filter ban). Idle (bloom ≈ 0) freezes the swing at dim — obvious delta
when playing (law 9).

### 7.3 Layer 2 — Clock curtain + breath (Bucket)

A translucent horizontal band drops from the top of the core viz, its lower edge mapping
`log(recon cutoff)`: Time short → curtain high (bright line visible), Time 2 s on Club
1100 → curtain swallows the tap tips (echoes literally drown). One `<rect>` with a height
tween; recomputed only on the geometry signature change (the :8056 sig check — free).
The compander breath: baseline glow opacity = `0.25 + 0.5·bloom_release_asymmetry` —
rises instantly, releases at the compander's own τ so the UI *breathes at the DSP's rate*.

### 7.4 Layer 3 — Pitch ladder (Shift)

Each tap offsets vertically by `n · semis · 1.1 px` (up-shift climbs, down-shift sinks —
the repeat ladder is drawn as an actual staircase) and carries a 1-px chevron glyph
(^, ^^ for octave; v down). See-Saw zig-zags; Haze doubles each tap with a ±1 px ghost.
Pure geometry at signature-rebuild time; zero per-frame cost beyond the existing flashes.

All three layers live inside `buildGeo`/`tick` (:8050-8090) under the existing no-early-
return rAF law and the `__cardOnly` fb90 guard (:8082).

---

## 8. §Interplay — the device in the chain

- **Unity-through:** POWER defaults OFF (:3483 — dry init). Switched ON at defaults
  (Mix 34 % equal-power ⇒ dry ×0.87 + wet ×0.50, fb 10 %): +0.4 dB program — within the
  house ±0.5 dB unity-through discipline. The §3.2 makeup keeps this true on every
  Type/Character (the old drive-dependent loop gain would have broken it per Character).
- **Spectrum downstream:** Bucket/Shift-down push energy below 1-2 kHz (feed a reverb →
  mud; the classic fix is the reverb's own low cut — presets in §9 pre-set it). Shift-up
  starves LF and feeds shimmer-verb style stacks beautifully (Delay → Reverb order).
- **Dynamics downstream:** the compander's release breath adds ~3 dB slow-envelope motion
  on tails — after a compressor (the upcoming device) it will pump the compressor; the
  classic ordering wisdom holds: **compressor BEFORE delay**. The 6-way `fxPerm_` chain
  (:7383-7391) already lets Max audition this.
- **Ordering wisdom (defaults):** Distortion → Delay → Reverb (echoes OF the distorted
  signal, tails smeared into space) = perm 2/3 family; delay-before-distortion turns
  repeat chains into sustain walls (a feature — preset "Sustain Wall" §9).
- **Stacking (the multi-device epic):** two delay instances at related divisions comb
  (1/8 + 1/8D is gold; 1/8 + 1/8 at slight detune is flange mush — document, don't
  prevent). 🚨 THE LANDMINE: any additional delay device instance must join **every**
  main-send exclusion sum — the fb305/fb338 law, sites `PluginProcessor.cpp:7159, 7358-7362`
  and the distortion twin at :7325-7333. The distortion bible §4.5 documents the exact
  two-line class of edit; a fourth device re-breaks all three if missed.
- **Feedback + freeze under the AMP env (law: everything follows Env 1):** the wet is a
  send off the voice mix — sound dies with the note by construction; self-oscillation at
  fb > 100 % decays once input stops feeding the compander/limiter chain only if fb ≤ 1.2
  with softClip — verified BIBO (§5.4). Nothing free-runs (Chaos leak per §3.4).

---

## 9. §Presets — factory sketches (DLY_PRESETS grammar, `index.html:7737`)

Format: `{name, k:[Time,Fdbk,Tone,Mix], d1:Character, pl:[Sync,Ping]}` + back deltas.

| # | Name | Type · Character | Values (front / notable back) |
|---|---|---|---|
| 1 | Analog Slap | Bucket · Short Clock | free 92 ms, fb 14, tone 55, mix 30 |
| 2 | Bucket Bounce | Bucket · Long Clock | 1/8D, fb 45, mix 36, Ping ON |
| 3 | Dark Dub | Bucket · Club 1100 | 1/4, fb 72, tone 30, mix 42, Low Cut 35 |
| 4 | Memory Lane | Bucket · Memory | 1/8, fb 38, mix 34, Mod Depth 15 (chorus rides) |
| 5 | Almost Gone | Bucket · Hot Bucket | 1/2, fb 96, mix 45 — parks at the self-osc lip |
| 6 | Underwater v2 | Bucket · Worn Cart | free 420 ms, fb 62, Hi Cut 28, mix 38 (upgrades the old BBD "Underwater") |
| 7 | Seasick | Drift · Chaos | 1/4, fb 40, Mod Rate 25, Mod Depth 55, mix 40 |
| 8 | Slice Clock | Drift · Square | 1/16, fb 55, Mod Rate 60, Mod Depth 70 — rhythmic teleport stutter |
| 9 | Roulette | Drift · S&H | 1/8, fb 50, Mod Depth 45, Ping ON |
| 10 | Siren Bend | Drift · Ramp Down | 1 bar, fb 65, Mod Rate 15, Mod Depth 85 — the destructive top (no playing safe) |
| 11 | Shimmer Stairs | Shift · Shimmer | 1/2, fb 60, Hi Cut 85, mix 44 |
| 12 | Basement Choir | Shift · Octave Down | 1/2D, fb 55, Low Cut 40, mix 40 |
| 13 | Dub Siren Fifths | Shift · Fifth Up | 1/4, fb 68, Ping ON, mix 45 |
| 14 | Sustain Wall | Bucket · Long Clock | 1/8, fb 88, mix 55 — documented for the Delay→Distortion perm |

Level spread gate (the fb345 lesson — flag near-twins and hot/quiet outliers): all 14
must land within ±3 dB of each other on the −26 dBFS chord probe before shipping.

---

## 10. §CPU — budget and tiering

Baseline: the certified device ≈ "nearly free" (2 lines, one-pole lattice, per-sample
smoothing; dst-family sweep measured the whole FX trio well under budget, fb343 −35 %
pass). Deltas per new type, per stereo sample:

| Type | Added work | Est. vs Digital |
|---|---|---|
| Bucket | 2 one-pole recon + 1 pre-LP + compander (4 MA + 2 sqrt + 1 div) + poly grit (3 MA) + tanh | ≈ +45 %, ≈ Tape+15 % |
| Drift | 1 LFO shape eval (branch + 1 sin worst case) | ≈ +6 % |
| Shift | +1 Hermite read (+2 during 10 ms re-sync fades) + pointer compares | ≈ +25 % |

One type active at a time (device law) → worst case ≈ Bucket ≈ still < the Diffuse
allpass path that already ships. **No oversampling anywhere** (§5.5). Coefficients at
block rate only (§5.8). No per-sample transcendentals except the single tanh (Bucket)
and the Drift sine — both already precedented in the shipped Tape path. Control-head
sleep (fb342 awake-head law): when POWER off and `dlyEnv_` < 1e-4 the insert already
early-outs (:7347). Nothing more needed.

---

## 11. §The DLY panel retirement — both options + the migration story

Max said everything below the chopper gets replaced — but the call is his (§14 Q1).

### Option A — RETIRE (recommended)

1. Remove the MoogDelay call site (`PluginProcessor.cpp:6715-6730`) + `moogDelay` member
   (`PluginProcessor.h:1499`, prepare :3823, reset :3996). `MoogDelay.h` stays in-tree as
   the port's reference source (delete only after Bucket/Drift/Shift are certified).
2. **KEEP every `DLY_*` param registered** (:1362-1455) — deleting APVTS params breaks
   old-session restore (the reverse of §3.5). They become dormant legacy params; UI
   relays (:9310-9318) go with the panel.
3. Remove the panel markup (:5540-5652), button (:5177), switcher branch (:12175),
   draw code (:16975-18100 region) — pure UI deletion, zero DSP risk.
4. **Migration shim** (one-time, on state load): if `DLY_MIX > 0.001` AND the tape section
   is on, translate into the device so old patches keep their echo:

| Old (DLY_*) | New (SYN_DLY_*) |
|---|---|
| TIME ms | TIME = ln(ms)/ln(8000), SYNC off (or SYNCDIV via the div map if DLY_SYNC was on: 1/32…1/2 → indices 16…4) |
| FEEDBACK f | FEEDBACK = f/1.2 |
| TONE ±1 | TONE = 0.5 + tone·0.5 |
| CHARACTER c | TYPE = Bucket; Character = nearest drive row (c<0.25→Long Clock, <0.6→Hot? no: 0.25-0.6→row 5, ≥0.6→Hot Bucket) |
| MOD>0.15 & wave∈{Sqr,S&H,Chaos} | TYPE = Drift, Character = wave, MODDEPTH = mod, MODRATE = (rate−0.05)/7.95 |
| PITCH≠OFF | TYPE = Shift, Character = {−12→0, −7→1, +7→4, +12→5} |
| MIX m (linear) | MIX = atan2(m, 1−m)/(π/2) (equal-power match) |
| WIDTH enum | Mono→WIDTH 0 · Stereo→WIDTH 0.625 · Ping→PING on |
| DUCK / FREEZE | DUCK direct / dropped (log it) |

   Precedence on conflicts (device already configured in the old session): the DEVICE
   wins; the shim only fills a device that was at defaults. POWER: set ON only if the
   shim actually migrated (never wake a dry patch).
5. CPU win: the tape-chain loses a per-sample 12-transcendental engine.

### Option B — KEEP BOTH (rejected but priced)

Panel stays for the tape-loop workflow; device gains the types anyway. Costs: the §1.1
CPU trap stays live, two delays with different laws confuse the product ("no doubles"
rule pressure), and the DLY panel's knob style is the old pre-v7 language. Only worth it
if Max still uses HOLD/∞ performance gestures on the front page — in which case port the
freeze law to a device pill instead (§14 Q4).

---

## 12. §Pitfalls — collected

1. **The −30 dB compander threshold** (§3.1) — the one that already shipped broken once.
2. **Small-signal tanh gain** un-cancelled → fb knob lies per Character (§3.2).
3. **Choice cardinality** — Type 4→6 must be append-only + every normalized UI write
   updated (§3.5); Character must STAY 8.
4. **Pitch re-sync splice clicks** — dual-tap crossfade mandatory (§3.3).
5. **Pitch below 40 ms = ring-mod trash** — keep the auto-bypass crossfade.
6. **Chaos walk free-runs / integrates DC into time** — leak it (§3.4).
7. **Two clocks drift apart** — one LFO phase, R at 90° (fb345 one-clock law).
8. **Per-sample biquad recompute** — block-rate coefficients only (§5.8).
9. **DC pump from asymmetric grit in the loop** — the AC-couple law (§5.6).
10. **Denormals** in compander envelopes + fade accumulators — flush (§5.6).
11. **Snapped Square/S&H tap = click** — 4 ms micro-glide (§4.2).
12. **Character hot-swap pops** (drive/ratio jumps) — 30 ms fade-swap (§5.2).
13. **Down-shift LF pileup** — Low Cut default nudge on Shift down-chars, knob stays live.
14. **Mono-sum**: width stays wet-only M/S (never in-loop); Ping alternation sums clean —
    no polarity tricks anywhere (DELAY-RESEARCH §5 law).
15. **Old sessions**: legacy DLY_* params must remain registered (§11.A.2).
16. **The exclusion-sum landmine** on any future extra delay instance (§8).
17. **Viz**: no early-return in rAF, no per-frame shadowBlur, taps[]↔els[] order pact
    (:8069 comment), `__cardOnly` guard — all already in the shipped viz; keep them.

---

## 13. §Hard-rule compliance checklist (laws 1-10, walked)

1. **Bus reality (−26 dBFS):** the compander is reference-free by design — immune (§3.1,
   §5.3). The only absolute threshold anywhere (duck −40 dB floor) is pre-existing device
   code. Drive rows voiced against −26 program (Character table drives are loop-side,
   level-independent by the tanh-normalized + makeup pairing). ✅
2. **Chassis fb275:** 2 dropdowns + 8 knobs + 11 params — untouched; the port adds ZERO
   params; Character/back-8 re-keyed per type like the distortion family pattern (§6). ✅
3. **Time law:** 4 bar → 1/256 — inherited unchanged (:3455-3461, :7216-7240). ✅
4. **Mix 100 % = fully wet** (equal-power, :7278) — inherited. **Type/Character switches
   fade-swap** (:7204-7210 + §5.2 Character fades) — never cut. ✅
5. **Params evolve 0→100, types night-and-day:** per-type discriminators with numeric
   gates (§4.1 centroid slope + breath, §4.2 ±700 cents vs 35, §4.3 F0 ladder);
   Drift Mod Rate re-tapered so the whole sweep reads; Character rows must pass the
   2.5× spread gate or die (§4.1). Max position destructive by design (Siren Bend,
   Almost Gone). ✅
6. **Nothing free-runs + loop gain law:** full ledger §5.4, max stable loop gain 1.2
   softClip-bounded; Chaos leaked; Pump is input-gated; freeze parked pending Max. ✅
7. **No clicks:** every new path glided (time repitch glide, Character fade, S&H micro-
   glide, pitch dual-tap, compander is gain-continuous); click-floor probes specified. ✅
8. **CPU:** block-rate coefficients, no oversampling verdict with reasoning (§5.5),
   one-type-at-a-time, budget table (§10). ✅
9. **Audible ⇒ visible + dramatic:** three per-type viz layers riding the existing
   certified timeline + bloom push; idle-dim/playing-bright preserved (§7). ✅
10. **Recycle first:** the entire plan is a recycle — MoogDelay.h algorithms into
    DelayEngine.h seams, existing chassis, existing viz, existing preset menu; inventory
    §15. ✅

---

## 14. §Open questions for Max

1. **Does the front-page DLY panel retire?** (§11 A vs B). You said everything below the
   chopper gets replaced — confirm the delay panel is in that blast radius, and whether
   the migration shim (A.4) should run silently or announce itself.
2. **Six types OK?** Digital · Tape · Bucket · Diffuse · Drift · Shift — or do you want
   the minimal port (Bucket rebuild only, Drift/Shift later)? Cardinality plan §3.5
   handles either.
3. **"Bucket" as the name** for the rebuilt BBD type (trademark-safe, mechanism-honest) —
   or keep the label "BBD" and only re-voice it? (Keeping the label = zero UI churn;
   renaming = clearer story next to Drift/Shift.)
4. **The Hold/∞ pill:** the freeze law is ported and documented (§1.1) but the front card
   has its two pills (Sync + Ping). Ship Hold as a third pill (breaks the 2-pill
   centerline rhythm), swap it in per-type, or park freeze entirely?
5. **Bucket compander breath depth** — voiced at ~3 dB (audible but polite). You killed
   the last compander for hiss+squash; the rebuilt one can't do either, but the *breath*
   is a taste call — want it hotter (Colour Copy-style obvious) or subtle?
6. **Shift's See-Saw / Haze rows** — keep both, or trade one for a "+2 semitone chromatic
   riser" (the dub-siren-adjacent trick)?
7. **Old "Underwater"-class DLY factory presets** — migrate by shim, or hand-revoice the
   handful on the new Characters (I recommend hand-revoicing; §9 #6 is the sketch)?

---

## 15. §Recycle inventory (verified by reading, file:line)

| Reused thing | Where | Used for |
|---|---|---|
| Hermite fractional read | `DelayEngine.h:230-248` | all reads incl. Shift pointer (identical poly at `MoogDelay.h:549-598`) |
| Per-sample smoothing lattice | `DelayEngine.h:141-150` | every ported param glide |
| Type pending-swap fade | `PluginProcessor.cpp:7204-7210` | Character fade-swap (§5.2) |
| 20-division sync resolve | `PluginProcessor.cpp:7216-7240` | unchanged |
| fbMk per-type makeup slot | `DelayEngine.h:198` | Bucket's `tanh(d)/d` (§3.2) |
| softClip loop bound | `DelayEngine.h:315-322` | all new types' stability ceiling |
| In-loop LC/HC one-poles | `DelayEngine.h:256-261` | unchanged; recon LP stacks under Hi Cut |
| Output tone tilt | `DelayEngine.h:263-271` | unchanged (TiltShelf superseded) |
| BBD time-tracking LP precedent | `DelayEngine.h:121-126` | replaced by the full recon law (same code shape, new constants) |
| Wet-only M/S + ping morph + duck | `DelayEngine.h:191-220` | unchanged |
| Denormal flush | `DelayEngine.h:330` | extended to new state |
| Recon/pre cutoff law | `MoogDelay.h:493-504` | Bucket bandwidth (§4.1) |
| LFO waveform set | `MoogDelay.h:512-527` | Drift Characters (§4.2) |
| Pitch pointer + auto-bypass | `MoogDelay.h:306-396` | Shift (§4.3, §5.7) |
| Freeze law | `MoogDelay.h:265-266,401-403,431` | parked for the Hold pill |
| Compander topology (structure only) | `MoogDelay.h:51-105` | rebuilt reference-free (§5.3) |
| Echo-timeline viz + bloom push | `index.html:8037-8090`, `__fxBloomDly` :8083 | the three §7 layers |
| Preset menu (.pmenu) + DLY_PRESETS | `index.html:7737-7741, 7865, 8230` | §9 presets |
| Cardinality-sensitive UI writes | `index.html:8236, 8270-8276, 8292` | the §3.5 lockstep edit list |
| fxPerm 6-way chain + exclusion sums | `PluginProcessor.cpp:7358-7362, 7383-7391` | §8 interplay, untouched |
| Legacy-anchor choice pattern | `PluginProcessor.cpp:3488-3495` (SYN_FX_ORDER) | the append-only Type migration precedent |

---

## 16. §Sources

**Repo (primary):**
- `Source/MoogDelay.h`, `Source/DelayEngine.h`, `Source/PluginProcessor.cpp`,
  `Source/ParameterIDs.hpp`, `Source/ui/public/index.html` — line anchors throughout.
- `Design/DELAY-RESEARCH.md` (fb296 consolidated delay research — Serum/Valhalla/Arturia/
  EchoBoy/u-he facts + the routing/timing laws).
- `Design/DISTORTION-BUILD-BIBLE.md` (family-keyed back-8, extremity/taper laws, §4.5
  exclusion-line trap), `Design/REVERB-BUILD-BIBLE.md` (Freeze-pill precedent).

**Web:**
- Raffel & Smith, *Practical Modeling of Bucket-Brigade Device Circuits*, DAFx-10 —
  https://colinraffel.com/publications/dafx2010practical.pdf (all §2.2 numbers; compander
  model; THD law; poly nonlinearity; variable-rate aliasing).
- Moogerfooger family history (MF-104/Z/SD/M/MSD chips, dates, LFO section, plugins) —
  https://en.wikipedia.org/wiki/Moogerfooger
- MF-104S plugin parameter reference (TIME ranges, Range octave law, LFO 6 waves
  0.05-50 Hz, Drive, CV set, Legacy/Analog/Modern tone, Loose/Strict, Spillover) —
  https://moogconnect.net/m/mfs/mf-104s.html
- Moog MF-104S product/store — https://software.moogmusic.com/store/mf-104s
- Arturia Delay MEMORY-BRIGADE manual (BBD Size 40-400/100-1000, chorus/vibrato, Delay
  Mode L/R / Ping-Pong / M/S, Blend law, input EQ, env follower, LFO) —
  https://dl.arturia.net/products/delay-brigade/manual/delay-brigade_Manual_1_5_0_EN.pdf
- u-he Colour Copy (varispeed clock, colour macros, LFO targets, ducking FB mode, freeze) —
  https://u-he.com/products/colourcopy/
- ValhallaDelay modes/controls — https://valhalladsp.com/shop/delay/valhalladelay/ ·
  https://valhalladsp.com/2019/04/16/valhalladelay-the-controls/ ·
  https://marulamusic.com/gear/valhalla-delay/
- Moog Matriarch stereo delay (MN3005, ≤700 ms, sync, ping-pong) —
  https://www.polynominal.com/moog-matriarch/
- Xfer Serum 2 manual hub — https://www.xferrecords.com/manual/serum-2/docs
  (delay control set + HQ facts carried from DELAY-RESEARCH.md §4).

*Written 2026-08-14. Builder-sufficient: every algorithm, constant, seam, migration row,
verify gate and UI site is specified above; no re-research required.*
