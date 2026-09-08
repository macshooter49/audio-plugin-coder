# Terrain Instrument — DELAY MOOG PORT PLAN
### The DLY panel (MoogDelay.h, MF-104S-inspired) → the FX Delay device, as new Types

**Status:** research complete + **ADVERSARIALLY AUDITED 2026-08-14** (every line anchor re-read
against the tree; the Raffel DAFx-10 PDF, the Moogerfooger history page and the MF-104S param
reference re-fetched). Corrections are marked **[AUDIT]** inline. Awaiting Max's scope call (§11).
Anything the audit could not confirm is marked **⚠️ UNVERIFIED** — do not quote those as fact.
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

## 0.9 🔑 THE BOUNDARY LAW vs the GRANULAR device *(added by the 2026-08-14 cross-bible sweep)*

Both devices own a long capture buffer and both have a Freeze. Neither bible stated the line, so
here it is — `GRANULAR-FX-BUILD-BIBLE.md` §0.0 carries the identical text:

```
DELAY (this device) = RECIRCULATION.  A small number of read heads at MUSICAL intervals, fed by a
                                      feedback loop. Its buffer is a LOOP; its identity is loop gain.
GRANULAR            = RE-READ.        Many independent short heads scattered over the SAME captured
                                      past, each with its own age / pitch / pan / window. Its buffer
                                      is an ARCHIVE; its identity is the grain cloud.
```

* **The two Freezes are different mechanisms and both are correct.** This device's ported Freeze
  law (`MoogDelay.h:265-266, 401-403, 431` — capability 5 above, parked for the Hold pill, §14 Q4)
  fades the *input* out and drives `effectiveFb → 1`, so a short musical loop recirculates forever:
  the buffer keeps moving and the *content* repeats. Granular's Freeze is a **write-blend** — the
  write head keeps advancing but stops taking new audio, so the *archive* is held while grains graze
  it. A frozen delay repeats a phrase; a frozen granular suspends a texture. **Do not implement
  either one with the other's mechanism**, and do not "unify" them into a shared helper.
* **The overlap is deliberate:** Granular's `Scatter` Type on a synced clock with long grains
  approaches a smeared delay, and this device's `Diffuse` Type approaches a wash. They meet in the
  middle from opposite directions — the "fewer, deeper" position, not duplication.
* **Neither device grows into the other.** The Delay never gets per-grain pitch/pan/window scatter;
  Granular never gets a tap/ping-pong/sync-division echo structure. Note this constrains capability
  3 (pitch-shifted feedback): a *per-repeat* transposition is delay behaviour and stays here;
  *per-grain* pitch scatter is Granular's and must not migrate into `DelayEngine`.
* Related boundary laws: `TAPE-BUILD-BIBLE.md` §0.2 (four tape surfaces — and note `DLY.Tape` is
  explicitly "a delay FLAVOR: one tanh + LP colour", never the tape *machine*).

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
| `Compander` | 51-105 | Peak follower 10 ms atk / 100 ms rel. **Threshold −30 dBFS**; compress-in 4:1 *above* it, expand-out 4:1 *below* it (`overDb·0.25`, `underDb·4`). Per-sample `log10` + `pow` ×2 channels ×2 stages. **This exact structure is the fb308 "echoes died + hiss pumped" bug** — root-caused in §3.1. **[AUDIT] Second, independent defect in the same block:** `compressIn` sits INSIDE the recirculating loop (:432-433 — it eats `input + feedback` on every pass) while `expandOut` is applied to the **wet output only** (:445-446). The round trip is therefore *never* closed: every recirculation compresses again with no matching expansion, so repeat *n* is compressed *n* times. Any port MUST place both stages inside the same loop (§4.1 does). |
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

`index.html:5540-5649` (**[AUDIT]** was written 5540-5652; the panel's closing `</div>` is 5649,
5651 starts the SVG defs): 8 knobs — TIME · FB · TONE · CHAR · MOD (waveform popover, 7 shapes
:5610-5616) · RATE · MIX · DUCK — plus segment pills PITCH (OFF/−12/−7/+7/+12), WIDTH
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
- **Processor block:** setup at `i==0` `PluginProcessor.cpp:7199-7276`. **[AUDIT] — the anchors
  in this bullet were all 3-6 lines high; re-read and corrected:** 🔑 **type clamp
  `if (dpend < 0 || dpend > 3) dpend = 0;` :7202** (this line was MISSING from the original plan
  and it silently forces any new Type back to Digital — see §5.1), type pending-swap fade
  :7204-7210, sync resolve with the 20-division `divMult` table :7217-7239 —
  **4 bar → 1/256, the house time law**, free time `pow(8000, t)` = 1 ms→8 s :7244,
  feedback ×1.2 amplification **:7260** (was ":7263"), Low/Hi Cut **:7262-7263** (was
  ":7265-7266"), Mod Rate 0.05-8 Hz **:7266** (was ":7271"), equal-power mix **:7274-7275**
  (was ":7278-7280"). Insert lambda `applyDly` :7345-7374 (main-send with the fb305/fb338
  exclusion sums :7358 / :7360); the 6-way `fxPerm_` serial switch :7383-7391.
- **Params:** `SYN_DLY_*` (`ParameterIDs.hpp:374-401`), layout `PluginProcessor.cpp:3447-3491`
  (**[AUDIT]** was ":3448-3487"; TYPE choice is :3448-3450, HQ closes at :3491, `SYN_FX_ORDER`
  follows at :3492-3500) — TYPE choice(4) · CHARACTER choice(8)
  `{Clean,Warm,Vintage,Modern,Lo-Fi,Bright,Dark,Wide}`
  · SYNCDIV choice(20) ×2 (L/R) · 13 floats · 6 route bools · SYNC/LINK/PING/POWER/HQ bools.
  **[AUDIT] float count is 13, not 12** (TIME, TIME_R, FEEDBACK, TONE, MIX, LOWCUT, HICUT,
  SPREAD, WIDTH, MODRATE, MODDEPTH, **WOW**, DUCK) — and **two of them have no control on the
  chassis**: `SYN_DLY_WOW` and `SYN_DLY_DUCK` are registered, default 0.0, and appear in
  neither the front-4 nor the back-8. `WOW` is therefore a **permanently-zero dead param**
  (it only ever mattered to Tape) — flagged under the "no dead params" law; §4.1 recycles it.
- **UI card:** DEVS entry `index.html:7483-7485` (front knobs Time/Fdbk/Tone/Mix, pills
  Sync+Ping, back = Character + Sync dropdowns, 8 knobs Low Cut · Hi Cut · Spread · Width ·
  Mod Rate · Mod Depth · Time L · Time R); presets `DLY_PRESETS` :7737-7741; **echo-timeline
  viz** :8037-8090 (geometry from REAL time/fb/ping/link, pulse sweeps at the echo rate,
  brightness rides the 60 Hz `__fxBloomDly` C++ push :8083 — the fb312/fb342 laws);
  restore list :7918-7924 (type decoded at :7926 as `Math.round(norm·(T.length−1))`);
  **[AUDIT] the TYPE write site is `index.html:8246`** —
  `window.__setSynParam(tpid, tn>1 ? t.selectedIndex/(tn−1) : 0)` with `tn = <select>.options.length`.
  The original plan listed :8236 / :8270-8276 / :8292 as "the cardinality-sensitive sites";
  those are real but they are the **SYNCDIV / SYNCDIV_R / TIME_R** writes (N = 20), not the Type
  write, and they were the only ones named. Both the Type write (:8246) and the Type read
  (:7926) derive N from the list itself, so they self-size — the actual trap is a **mismatch
  between the JS `DEVS[].types` array and the C++ `StringArray`**, plus host automation
  (§3.5, rewritten).

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
- **MF-104Z** ~1 s · **MF-104SD** 1.4 s, 250 units (⚠️ UNVERIFIED: the "2005" release year for
  both — the source page gives the unit counts and delay lengths but not those dates).
  Also documented: **MF-104MSD**, 1.2 s, 560 units + ~30 Gearfest units.
- **MF-104M (announced June 2012)** — **[AUDIT] FOUR MN3008 BBDs, not two** (the original plan
  said "2×MN3008"; the Moogerfooger history page states "uses 4 MN3008s"). 800 ms max,
  **the 6-waveform LFO modulation section** (the defining addition), tap tempo, MIDI,
  spillover mode, dual outs. "The last of its kind." Line discontinued **28 Aug 2018**.
- **Matriarch (2019)** — the same lineage on **MN3005** reissue chips: stereo analog delay,
  ≤700 ms, MIDI-syncable, stereo/ping-pong. DFAM/Grandmother carry mono cousins.
  ⚠️ UNVERIFIED (search budget exhausted before the Matriarch source could be re-fetched) —
  do not cite the chip type or the 700 ms figure as fact without checking.
- **MF-104S plugin (2022, Moogerfooger Effects Plug-ins)** — the direct model for
  `MoogDelay.h`. Verified parameter set (moogconnect.net): TIME **SHORT 8-400 ms
  (brighter) / LONG 16-800 ms (darker)** — **the Range switch shifts everything in the
  loop by an octave** (clock doubling halves delay and doubles bandwidth — the origin of
  our Pitch-in-feedback idea — the source's exact wording is "switching halves or doubles the
  delay time **and pitch** of the loop sound"); FEEDBACK self-oscillates above ~8/10 (≈3 o'clock);
  **LFO: 6 waveforms
  (Sine/Tri/Square/Ramp/Saw/S&H), 0.05-50 Hz, Amount onto delay time**; TIME SYNC caps at
  800 ms; DRIVE (input
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
  f_clk = 6826⅔ Hz → the input must be band-limited to **3413⅓ Hz** (the paper's own worked
  example — verbatim). **[AUDIT] the MF-104M line was wrong**: it is **4×MN3008 = 8192 stages**,
  so at 800 ms series-chained f_clk = 5120 Hz → **Nyquist 2560 Hz**; only if the four chips are
  wired as two series pairs (4096 stages per path) do you get the plan's original
  f_clk 2560 / Nyquist 1280. ⚠️ UNVERIFIED which topology the MF-104M uses — quote the range
  (**1.3–2.6 kHz at 800 ms**), not a single number. Either way the point stands: long BBD
  repeats are dark because of a hard sampling limit, not a tone knob.
- **Filters:** 3rd-order Sallen-Key anti-alias in + one 3rd-order + one 2nd-order
  reconstruction filter in series out; cutoff chosen between **⅓ and ½ of f_clk**, as low as
  **1.5 kHz** in long echo circuits. → `MoogDelay.h`'s `20000·(50/ms)` law = 1000/ms kHz,
  which for a 4096-stage line is **0.488·f_clk** (**[AUDIT]** the plan said "≈0.4·f_clk" —
  recompute: f_clk = 4096/(2·ms/1000) = 2.048e6/ms Hz, so cutoff/f_clk = 1e6/2.048e6 = 0.488).
  That sits at the **very top** of the paper's ⅓…½ window — physically legal, deliberately
  the brightest legal choice. Keep it, but know that dropping K toward ⅓ is the knob for
  "darker chip" Characters (§4.1) and is what makes Club 1100's ×0.7 authentic rather than
  arbitrary.
- **Compander:** NE570/571, compression ratio **2** (not 4), gain from a full-wave-rectified
  one-pole average, τ = 10000·C_rect with C = 0.22-1 µF → **τ = 2.2-10 ms**. Feedforward
  expander `f(x) = avg(|x|)·x`; feedback compressor `f(x) = x / avg(|f(x)|)`. **Reference-free
  — there is no threshold anywhere.** (§3.1: our −30 dBFS threshold version is the bug.)
- **Frequency-dependent insertion gain:** 0…+2 dB at LF falling to **−4…−6 dB at Nyquist**
  for any clock rate.
- **Noise:** SNR ≈ 60 dB, further suppressed by the compander → "reasonably treated as
  imperceptible" — supports fb308's NO-NOISE ruling; we do not add hiss.
- **Distortion:** THD ≈ **1.01^(N/1024) − 1** (≈1 % per 1024 stages), *level-independent*
  (charge-transfer loss, not clipping) — **verified verbatim in the paper**. Third-order fit,
  also verbatim: `f(x) = x − a·x² − b·x³ + a` for −1<x<1, `1−a−b` for x>1, `−1−a+b` for x<−1,
  with **a = 1/8, b = 1/18**. The `+a` is in the paper on purpose ("ensures the signal will
  average around 0" against the −a·x² term). **[AUDIT] — two implementation traps the plan
  did not flag, both fatal in a feedback loop:**
  1. **The paper's piecewise is DISCONTINUOUS.** f(1) = 1 − a − b + a = 1 − b = **0.944**, but
     the stated clamp above +1 is 1 − a − b = **0.819** → a **0.125 step** at |x| = 1, i.e. a
     click generator every time a repeat crosses unity. Use the continuous clamps
     **±(1 − b) = ±0.944**.
  2. **`+a` is a level-independent DC injection.** f(0) = **+0.125 = −18 dBFS of DC**, present
     even with **no input** — inside a recirculating loop at fb→1 that integrates. It also
     fails the fb345 SILENCE metric (idle < −90 dBFS) on its own. The paper's intent
     (zero-mean output) is satisfied far more safely by **dropping the `+a` constant and
     letting the always-on in-loop DC blocker (§5.6) remove the −a·mean(x²) offset** — which
     is signal-dependent anyway, so a fixed `+a` only cancels it exactly at one level.
     **Ship `y = x − x²/8 − x³/18`, clamped ±0.944, with the 5 Hz in-loop HP ALWAYS ON for
     Bucket.** (§5.6 previously made that HP conditional on Low Cut < 30 Hz — corrected.)
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

**[AUDIT] — the code block originally printed here did NOT invert.** It used `√env` on *both*
stages: compress `y = x/√env_in`, expand `y = x·√env_out`. Walk it with a steady amplitude A
(ref = 1): the compressor outputs A/√A = **√A** ✓ (out_dB = in_dB/2, ratio 2 ✓); the expander
then sees B = √A and outputs B·√B = **A^0.75** ✗ — that is ratio 1:1.5, not 1:2, so the round
trip is `in_dB → 0.75·in_dB`, i.e. a **level-dependent 6.5 dB error at −26 dBFS** program and a
different error at every other level. It also directly contradicts §3.1's own verify gate
("must match within ±0.5 dB") and §5.4's ledger entry `comp_roundtrip(=1)`. The paper's own
forms (quoted correctly two sections earlier in §2.2 and then not used) DO invert exactly —
feedforward expander `f(x) = avg(|x|)·x`, feedback compressor `f(x) = x / avg(|f(x)|)`.
Corrected law:

```
ref = 1.0                                          // full-scale internal reference; the round trip is
                                                   // exact at ANY program level ⇒ bus-independent by construction
env(n)  = env(n−1) + k·(|x(n)| − env(n−1))         // full-wave avg, τ ≈ 5 ms (k = 1−e^(−1/(fs·0.005)))
envC    = max(env, ε),  ε = 10^(−60/20) = 1e−3     // gain freeze floor at −60 dBFS (no noise pump on silence)

comp in   : y = x · sqrt(ref / envC_in)            // ratio 2:1   → out_dB = in_dB/2      (amp A → √A)
expand out: y = x · (envC_out / ref)               // ratio 1:2   → out_dB = 2·in_dB      (amp √A → A)  ✓ EXACT inverse
```

(`envC_in` averages the compressor's *input*; `envC_out` averages the expander's *input*, i.e.
the compressed signal. If you prefer the paper's literal feedback compressor, track `|y|`
instead and use `y = x/envC_y` — same ratio, marginally different attack shape; the
feedforward form above is the cheap one and is what §5.3 costs out.)

Round-trip identity is now exact for steady state, while the two audible signatures survive:
**attack overshoot bloom** (env lags transients → first ms passes hot) and **release
breathing** on decaying tails. No `log10`/`pow` per sample (the in-tree version burns 4
transcendentals/sample), no threshold, no bus dependency. Feedback survives because the round
trip is gain-neutral — and, per the §1.1 audit note, **both stages must sit inside the same
recirculating path** or the "neutral round trip" is a fiction (that is the second half of the
fb308 bug).
**Verify gate:** loop-gain sweep with compander on vs off must match within ±0.5 dB at
fb = 40 % (echo count identical); the pump must show as ≥ 3 dB of gain modulation on a
−26 dBFS 4 Hz AM probe (audible breath) — the honest AM probe per the fb345 probe-craft.

### 3.2 🔑 Small-signal loop gain of the normalized tanh (the "mosquito buzz" math)

`bbdSaturate(x,d) = tanh(x·d)/tanh(d)` has derivative at 0 of **g₀ = d/tanh(d) ≥ 1**:

| Character drive d | g₀ = d/tanh(d) | fb knob where loop gain = 1 (fb×1.2 law, :7260) |
|---|---|---|
| 0.6 (char 0) | 1.117 | 0.746 → knob ≈ 62 % |
| 1.2 | 1.437 | 0.580 → knob ≈ 48 % |
| 1.8 | 1.903 | 0.438 → knob ≈ 37 % |
| 2.4 (char 1) | 2.437 | 0.342 → knob ≈ 29 % |

The in-code comment (`MoogDelay.h:398-400`, "at char=0, gain ~0.85x") is a large-signal
estimate; the SMALL-signal gain is what governs the tail of a decaying repeat chain, and
it exceeds 1 at every drive. The old MF port "mosquito buzz" was this: tails decay into
the linear region where the loop quietly has more gain than the knob says.
**[AUDIT] — the original law here was `fbMk_bucket = tanh(d)/d` (exact cancellation), and it
is wrong twice over.**

*First, the knob never reaches 1.2.* The engine hard-caps the loop coefficient at **0.98**
before the makeup (`DelayEngine.h:193`: `fb = fbCur > 0.98f ? 0.98f : fbCur`), on top of the
setter clamp 1.15 (:87). So the reachable ceiling is `0.98 · fbMk · g`, never `1.2 · fbMk · g`.
The knob saturates at **knob ≈ 81.7 %** (0.98/1.2); the top 18 % of the Fdbk sweep is a
**plateau** — which is itself a law-5 violation on the *shipped* device, pre-existing debt
worth fixing in the same pass (raise the cap to 1.15 and let softClip do the bounding, or
re-taper the knob so 100 % lands at the cap).

*Second, exact cancellation makes Bucket unable to self-oscillate.* With
`fbMk·g₀ = tanh(d)/d · d/tanh(d) = 1`, max loop gain = `0.98 · H_filters` — **strictly < 1 at
every setting**, so Bucket's echoes always decay. That directly contradicts §9 preset #5
("parks at the self-osc lip") and #14 ("Sustain Wall", fb 88).

**THE CORRECTED LAW:** cancel g₀ *and then apply the house makeup headroom*, so Bucket lands
on Digital's certified feedback feel at every Character:

```
fbMk_bucket(char) = 1.28 · tanh(d)/d              // 1.28 = the shipped Digital makeup (DelayEngine.h:198)
loop gain          = min(0.98, knob·1.2) · fbMk_bucket · g₀_sat(d) · H_lc·H_hc·H_recon · comp_roundtrip(=1)
                   = min(0.98, knob·1.2) · 1.28 · H_filters          ← drive-independent ✓
unity at            knob ≈ 65 % / H_filters ;  ceiling 1.254·H_filters at knob ≥ 81.7 %
```

The fb knob now means the same thing at every drive AND at every Type, and the ±1.4 softClip
(`DelayEngine.h:315-322`) turns the >1 regime into bounded, musical self-oscillation.

⚠️ **The claim "unity lands at knob ≈ 83 %, matching the certified Tape behavior" was FALSE.**
Tape's saturator is **not** normalized — `tape()` is `tanh(drive·(x + 0.06x²))` ×0.86 with
`drive = 1.4 + 1.2·(character_ & 1)` (`DelayEngine.h:274-280`), so its small-signal loop gain
is `0.86·drive` = **1.204 on even Characters, 2.236 on odd ones**. Tape reaches unity at knob
≈ **69 %** (even) and ≈ **37 %** (odd) — the Fdbk knob currently means two different things
depending on Character parity, a second piece of pre-existing debt to log alongside the §4.0
DC-pump note. Bucket at 65 % is the *closest* honest match; do not describe it as "matching
Tape".

**Max stable loop gain: 1.254·H_filters (knob ≥ 81.7 %), softClip-bounded; BIBO by the same
argument as fb308.**

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

Adding Types changes `SYN_DLY_TYPE` from choice(4) → choice(6). **[AUDIT] — the mechanism
described here was wrong and the rule it derived violates rack law C.**

*What is actually true.* The UI writes and reads the choice NORMALIZED, but **both sides derive
N from the list itself** — write `index.html:8246` uses `<select>.options.length`, read
`:7926` uses `DEVS[].types.length`. They self-size. Likewise `Math.min(i, opts.length−1)` at
:7561-7562. So there is no "list of write sites that must move in lockstep". The three sites
the plan named (:8236, :8270-8276, :8292) are the **SYNCDIV / SYNCDIV_R / TIME_R** writes
(N = 20) and are unaffected by a Type change.

*The two real hazards.*
1. **JS↔C++ list-length mismatch.** If `DEVS[].types` (index.html:7483) and the C++
   `StringArray` (PluginProcessor.cpp:3448-3450) disagree on length, every normalized write
   lands on the wrong index — silently, with no error. They must be edited in the same commit.
2. **🔑 Host automation lanes break — and APVTS state does not.** APVTS stores an
   `AudioParameterChoice` as its *un-normalised* value (the index), so a saved session with
   Diffuse (3) still reads 3 after the list grows ✓ — the plan's claim on this point is
   correct. But a **DAW automation lane** stores the normalized float. Recorded under N = 4:
   Tape = 0.333 → under N = 6 reads `round(0.333·5)` = **2 (Bucket)**; BBD 0.667 → **3
   (Diffuse)**; Diffuse 1.0 → **5 (Shift)**. Appending does *not* save you here. This is
   exactly why **fb342 law C exists: choice cardinality is fixed at birth.**

**Corrected rules:**
- Since the cardinality has to change anyway, **change it ONCE and size it for the final
  roster** — declare the list at its end-state length now, with any not-yet-built entries
  present but disabled/hidden in the UI (law C, the `SYN_FX_ORDER` precedent at
  `PluginProcessor.cpp:3492-3500`). Do **not** plan a second 6→7 bump later.
- Keep indices 0-3 in place so APVTS session restore is untouched.
- `Character` stays **exactly choice(8)**, re-labeled per type (zero cardinality change).
- Bump `TERRAIN_BUILD`; verify by (a) loading a pre-port session with each of the 4 legacy
  types selected, and (b) **replaying a pre-port automation lane on SYN_DLY_TYPE and
  documenting the remap in the release note** — it cannot be fixed, only announced.

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
- **Diffuse (3)** — 4-allpass smear (:296-313). Unchanged. **[AUDIT]** for Diffuse, **Mod Depth
  is the diffusion amount**, not chorus depth: `amt = 0.35 + 0.65·modDepthTgt` (:311), relabeled
  in the UI. §6.2's back-8 table said "chorus depth" for Diffuse — corrected there. ⚠️ Note
  :311 reads `modDepthTgt` (the **un-smoothed target**), not `modDepthCur` — a zipper on a
  dry↔wet crossfade, i.e. a live no-clicks-law violation on the shipped device. One-character
  fix; take it in this pass.

### 4.1 BUCKET (index 2, rebuilt) — the full MF-104/BBD physics port

*Lineage: MF-104M · MN3005/3008 · NE570 · Raffel & Smith DAFx-10 · Colour Copy.*

**Recipe** (replaces `bbd()` at `DelayEngine.h:287-293`; per-channel state ~6 floats):

```
write side: x → compand_in (§3.1, corrected) → preLP one-pole @ min(15k, recon·1.4) → buffer
read  side: tap → reconLP ×2 (two one-poles, 12 dB/oct) @ recon(char, timeMs)
            → BBD grit: y = x − x²/8 − x³/18, clamped ±0.944   // [AUDIT] no +1/8 constant (§2.2)
            → drive tanh(y·d)/tanh(d)
            → DC block: one-pole HP @ 5 Hz, ALWAYS ON for Bucket (§5.6)
            → compand_out (§3.1) → (this is fL/fR into the shared feedback write)
recon(char, ms) = clamp( K_char · (50/ms) · 20000 , floor_char , ceil_char )   // §1.1 / §2.2 law
fbMk_bucket     = 1.28 · tanh(d)/d                                             // §3.2 corrected law
```

**[AUDIT] two seams this recipe must not skip:**
- **`setCharacter` has NO upper clamp** — `DelayEngine.h:83` is
  `character_ = c < 0 ? 0 : c;`. The shipped types get away with it (`character_ & 1`,
  `character_ % 3`); indexing a `static constexpr` 8-row tuple table with it is an
  **out-of-bounds read**. Clamp to 0-7 in the setter as part of this port.
- Both compander stages sit **inside** the loop above, so `comp_roundtrip = 1` in §5.4 is
  earned rather than assumed (§1.1 audit note).

**Recycle opportunity (zero new params):** Character rows 3 and 6 below want baked chorus/wow.
`SYN_DLY_WOW` is already registered and is currently a **dead, permanently-zero param** (§1.3)
— drive the Worn Cart / Memory wobble from it and give the dead param a job, rather than
hard-coding constants.

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
the tap swings ±150 ms.

**[AUDIT] the pitch-excursion expression printed here ("ratio 1±0.30·2π·rate·T…") dropped
`depth` and got the sign wrong, and it hides a stability hole.** A modulated tap repitches by
the tap's *velocity*: for τ(t) = D·(1 + 0.3·depth·lfo(t)),

```
v  = dτ/dt = 0.3 · depth · 2π · rate · D · lfo'(t)        (samples per sample, peak at |lfo'| = 1)
ratio = 1 − v          cents = 1200·log2(1 − v)
```

`v` scales with **rate × D**, and both are large here: Mod Rate reaches 8 Hz and Time reaches
8 s, so peak `v` reaches `0.3·1·2π·8·8 ≈ 120`. Any `v > 1` means **the read tap outruns the
write head and reads unwritten samples**; `v > 1` also inverts playback. MoogDelay's only
guards are a 1-sample floor (:287) and the 5 ms glide (:289) — the glide caps slew near
30 samples/sample at 48 k, nowhere near enough. **Mandatory law for Drift:**

```
v_max = +0.5  (≤ 1 octave up)   v_min = −1.0  (≤ 1 octave down)
per sample: clamp the smoothed tap delta to [v_min, v_max]         // 2 compares, the hard safety net
per block : depth_eff = min(depth, v_max / (0.3·2π·rate·D))        // so the KNOB still evolves 0→100
            (report depth_eff to the viz so the ghost-swing shows the real, scaled depth — law 9)
```

Without the block-rate auto-scale the depth knob would simply plateau at high rate×time (law 5
violation); without the per-sample clamp the type is unstable. Ship both.

One phase accumulator, R at +90° (§3.4). Waveform = **Character**:

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

**Discriminator:** F0-deviation depth in cents on a 220 Hz probe through the wet path.
**[AUDIT] the probe conditions must be pinned or the number is meaningless** — cents depends on
`rate × Time`, not on depth alone. Pin it: **Time 500 ms, Mod Rate 0.7 Hz, Mod Depth 50 %,
Sine** ⇒ `v = 0.3·0.5·2π·0.7·0.5 = 0.33` ⇒ **≈ −700 cents on the falling half / +590 on the
rising half**, where Tape's Wow at the same probe maxes near ±35 cents. An order of magnitude
apart = night-and-day vs Tape by construction. Mod Rate/Depth become THE hero
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

0. 🔑 **[AUDIT] THE SEAM THE PLAN MISSED — `PluginProcessor.cpp:7202`:**
   `if (dpend < 0 || dpend > 3) dpend = 0;`. This runs *before* the engine ever sees the type,
   so selecting Drift or Shift would silently snap the device back to **Digital** with no
   error anywhere. Widen it with the same edit as seam 1. (There is no matching clamp bug on
   Character — but see seam 3a.)
1. `DelayEngine::setType` clamp 0-3 → 0-5 (`DelayEngine.h:82`).
1a. `DelayEngine::setCharacter` (`DelayEngine.h:83`) has **no upper clamp** — add
   `c > 7 ? 7 : c` before any tuple-table indexing (§4.1).
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
| Time | sync: 20-division table `PluginProcessor.cpp:7217-7239` (4 bar→1/256); free: `pow(8000,t)` ms (:7244) | per-sample one-pole `smCoef` ≈ 15 ms → repitch glide (`DelayEngine.h:142-143`) — BBD-authentic; keep |
| Feedback | `knob·1.2` (**:7260**) → engine clamp 1.15 (:87) → **in-loop cap 0.98** (:193) + softClip. **[AUDIT] the 0.98 cap makes the top 18 % of the knob a plateau** (law 5) — raise it to 1.15 and let softClip bound, or re-taper so 100 % lands on the cap | 15 ms ramp |
| Tone | output tilt ±85 % @ 760 Hz (:263-271) — NOT in loop (fb310) | 15 ms |
| Low/Hi Cut | `20·50^t` Hz / `1200·15^t` Hz (**:7262-7263**), in-loop one-poles | block + smoothed state |
| Spread | R = L·(1+0.35·s) linked (:107-111) | 15 ms |
| Width | wet-only M/S 0-1.6 (:209-211) | 15 ms |
| Mod Rate | 0.05-8 Hz (**:7266**), currently **linear**. **Drift: re-taper to `0.05·160^t` Hz** (same 0.05-8 endpoints, exponential) — the linear taper wastes the bottom half where vibrato lives (fb325 taper law) | phase-continuous (one clock) |
| Mod Depth | 0-1. Drift = ±30 % of Time, auto-scaled by the §4.2 velocity law; **Diffuse = allpass smear amount `0.35+0.65·d` (:311), NOT chorus**; Digital/Tape/Bucket/Shift keep ±6 ms chorus | 15 ms (**Diffuse currently uses the un-smoothed target — fix, §4.0**) |
| Time L/R | fb306 link grammar unchanged | 15 ms |
| Character | choice(8) — **fade-swap on change**: reuse the type-pending fade (:7204-7210) at Character granularity for Bucket/Shift (a drive or ratio jump is audible); Drift waves may hot-swap (phase-continuous) | 30 ms fade |

### 5.3 The compander (final form) — §3.1's law, per channel, Bucket only

State: 2 envelopes/channel — **recycle the four already-declared-but-dead members
`compEnvL/compEnvR/expEnvL/expEnvR` (`DelayEngine.h:358`, reset :76) and `compCoef` (:135)**,
left behind when fb308 removed the old compander. Zero new state. ⚠️ While there, fix the two
now-false claims in the engine's own header comment (`DelayEngine.h:6-7` and :19-20 still
advertise "BBD companding" / "companding pump" as shipped features).

Cost with the corrected law (§3.1): **1 sqrt + 1 div + ~3 mult-adds per sample per channel**
(the expander is a plain multiply — no second sqrt; `std::sqrt` is SIMD-friendly; NO log/pow).
Round-trip gain identity is exact by construction; the freeze/idle guard is the ε-clamp (gain
frozen below −60 dBFS → no pump on silence → the fb345 SILENCE-metric gate passes: idle output
< −90 dBFS, **provided the grit's DC constant is dropped per §2.2** — otherwise this gate fails
outright at −18 dBFS).

### 5.4 Stability (the loop-gain ledger — law 6)

Every gain stage inside the recirculating path, per type:

```
Digital  : F · 1.28                                              max 1.254·H
Tape     : F · 1.0 · 0.86·drive · H_7k2      drive = 1.4 + 1.2·(char&1)
           → even char  0.86·1.4 = 1.204     max 1.180·H   (unity at knob ≈ 69 %)
           → odd  char  0.86·2.6 = 2.236     max 2.191·H   (unity at knob ≈ 37 %)  ⚠ parity jump
BBD today: F · 1.00 · drive                  drive = 1.15 + 0.45·(char%3) → 1.15 / 1.60 / 2.05
           → max 1.127…2.009·H               (also unnormalized ⇒ Character changes the fb law)
Bucket   : F · [1.28·tanh(d)/d] · [d/tanh(d)] · H_recon · comp_roundtrip(=1)
           = F · 1.28 · H                    max 1.254·H   ✓ drive-INDEPENDENT (the whole point)
Diffuse  : F · 2.15 · allpass(unitary)                           max 2.107·H
Drift    : = Digital (mod moves the tap, adds no gain — but see the §4.2 tap-velocity clamp,
             which is a stability law about the READ POINTER, not about gain)
Shift    : = Digital path gains; energy migrates spectrally per §4.3
```

**[AUDIT] this ledger was rewritten.** The original used `fb·1.2` as the loop coefficient — the
engine caps it at **0.98** (`DelayEngine.h:193`) — and credited Tape with a *normalized*-tanh
gain of "≈1.35"; Tape's `tanh(drive·(x+0.06x²))·0.86` is **not** normalized, so its small-signal
gain is `0.86·drive`. Above, `F ≡ min(0.98, knob·1.2)` and `H ≡ H_lc·H_hc·(type LP)`, all ≤ 1.

Max stable loop gain across the roster: **≈2.1 (Diffuse)**, **1.254·H for Digital/Bucket**,
**up to 2.19·H for Tape on odd Characters**. softClip past ±1.4 converts every >1 regime into
bounded self-oscillation (the certified fb308 BIBO argument). Freeze (if
shipped) pins loop gain to exactly 1.0 with input faded — bounded by construction. The two
unnormalized legacy saturators (Tape, BBD-today) are why the Fdbk knob currently means
different things on different Characters; Bucket is the first type where it does not (§3.2).

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
- DC: one shared in-loop DC block (one-pole HP @ 5 Hz). **[AUDIT] for Bucket it must be
  ALWAYS ON, not "engaged whenever Low Cut < 30 Hz"** — the grit's even term contributes
  −a·mean(x²), a *signal-dependent* DC that no fixed constant cancels (and if you ship the
  paper's `+a` literally you also inject +0.125 = −18 dBFS of DC at silence; §2.2 says drop
  it). For **Tape** the conditional engagement is fine (its `0.06x²` pump only matters when
  the low cut is out of the way). Costs 1 mult-add.
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

**[AUDIT] the "zero new knobs" claim holds — verified against the shipped chassis.** The
11-param count, named, exactly as the tree has them:
**Back: 2 dropdowns** — `Character` (`SYN_DLY_CHARACTER`, choice 8) + `Sync`
(`SYN_DLY_SYNCDIV`, choice 20) — **+ 8 knobs (4×2)**: Low Cut · Hi Cut · Spread · Width ·
Mod Rate · Mod Depth · Time L · Time R (`index.html:7485`, verbatim). **Front hero knobs:**
Time · Fdbk · Tone **+ Mix** (`:7484`), **pills** Sync · Ping. Every new capability lands on an
existing control: Bucket's chip voicing → Character; Drift's waveform → Character, its rate/depth
→ the existing Mod Rate / Mod Depth; Shift's interval → Character. **Nothing is added; the only
APVTS change is the Type list's length** (a construction-time layout change, not runtime
parameter creation — rack law B satisfied).
⚠️ Two registered floats sit **outside** this chassis with no control at all: `SYN_DLY_WOW`
(permanently 0 — dead) and `SYN_DLY_DUCK` (deliberately hidden, §6.3). Also note a live
UI/param default mismatch: the `DEVS` model paints Mod Depth at **30** (`:7485`) while the
APVTS default is **0.0** (`PluginProcessor.cpp:3479`) — cosmetic only (the reopen restore
overwrites it), but it makes the card lie for one frame on first paint.

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

> 🔧 **[CROSS-BIBLE AUDIT 2026-08-14] CHASSIS CORRECTION — `Type` is the HEADER PILL, not back-d1.**
> Verified in the shipped tree: on Reverb, Delay **and** Distortion, `*_TYPE` renders in the header
> `.fxr-type` `<select>` on the card centerline (`index.html` `DEVS[].tp` +
> `Design/fx-back-panel-mockup.html`); the two **back** dropdowns are `Character` + a second
> selector (`Mod Mode` / `Sync` / `Quality`). Spending back-d1 on `Type` duplicates the header pill
> — the most visible label the card has — and silently throws away a back dropdown this device is
> entitled to. Move `Type` to the header, slide `Character` to back-d1, and back-d2 is free.
> Full ruling (incl. that the honest knob count is **12** = 3 heroes + Mix + 8 back, not the "11"
> four bibles reconstructed four different ways): `FX-CHAIN-BIBLE.md` §7.1.

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
| 6 | Mod Depth | Digital/Tape = chorus depth; **Diffuse = allpass smear amount** (`0.35+0.65·d`, :311) — **[AUDIT]** not chorus | chorus depth | **±30 % Time, velocity-scaled (hero)** | detune depth ±25 cents |
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
  main-send exclusion sum — the fb305/fb338 law, sites `PluginProcessor.cpp:7159` (reverb),
  **`:7326`** (distortion) and `:7358` (delay), each with its R twin two lines below. The
  distortion bible §4.5 documents the exact two-line class of edit; a fourth device re-breaks
  all three if missed. **This port adds no bus, so the sums are untouched.**
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
| Bucket | 2 one-pole recon + 1 pre-LP + compander (**1 sqrt + 1 div + ~3 MA**, §5.3 corrected) + poly grit (3 MA) + DC block (1 MA) + tanh | ≈ +40 %, ≈ Tape+12 % |
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
| TIME ms | TIME = ln(ms)/ln(8000), SYNC off (or SYNCDIV via the explicit map below if DLY_SYNC was on) |
| FEEDBACK f | FEEDBACK = f/1.2 |
| TONE ±1 | TONE = 0.5 + tone·0.5 |
| CHARACTER c | TYPE = Bucket; Character = nearest drive row (c<0.25→Long Clock, <0.6→Hot? no: 0.25-0.6→row 5, ≥0.6→Hot Bucket) |
| MOD>0.15 & wave∈{Sqr,S&H,Chaos} | TYPE = Drift, Character = wave, MODDEPTH = mod, MODRATE = (rate−0.05)/7.95 |
| PITCH≠OFF | TYPE = Shift, Character = {−12→0, −7→1, +7→4, +12→5} |
| MIX m (linear) | MIX = atan2(m, 1−m)/(π/2) (equal-power match) |
| WIDTH enum | Mono→WIDTH 0 · Stereo→WIDTH 0.625 · Ping→PING on |
| DUCK / FREEZE | DUCK direct / dropped (log it) |

   **[AUDIT] the sync-division map was written as a contiguous range ("1/32…1/2 → indices
   16…4"), which it is not** — the old 9-entry list (`PluginProcessor.cpp:1439-1442`:
   `1/32, 1/16T, 1/16, 1/8T, 1/8., 1/8, 1/4T, 1/4, 1/2`) and the new 20-entry list are ordered
   differently, and index 14 (`1/16D`) has no old counterpart. Implement it as a literal table:

   ```
   static constexpr int kOldDivToNew[9] = { 16, 15, 13, 12, 11, 10, 9, 7, 4 };
   //  old:  0=1/32  1=1/16T  2=1/16  3=1/8T  4=1/8.  5=1/8  6=1/4T  7=1/4  8=1/2
   //  new: 16=1/32 15=1/16T 13=1/16 12=1/8T 11=1/8D 10=1/8  9=1/4T  7=1/4  4=1/2
   ```
   (Old default was index 5 = `1/8` → new index 10 = `1/8`, which is also the new default —
   so an unmoved old patch migrates to an unmoved new one. Good sanity check.)

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

**[AUDIT] — pitfalls the original list did not contain (all measured against the tree):**

18. 🔑 **The processor's own type clamp, `PluginProcessor.cpp:7202` (`dpend > 3 → 0`).** Widen
    it or Drift/Shift silently play as Digital. This is the single most likely way to "build
    clean and ship nothing".
19. 🔑 **The compander's compress and expand must be in the SAME loop.** MoogDelay compresses
    inside the loop and expands only on the output (§1.1) — compounding compression per repeat.
    Half of fb308's "echoes died" is this, not the threshold.
20. 🔑 **A ratio-2 expander is `x·env`, NOT `x·√env`.** The √ form (which the plan originally
    specified) is ratio 1:1.5 and does not invert the compressor (§3.1).
21. 🔑 **The Raffel polynomial's `+a` term is a −18 dBFS DC source at silence**, and the paper's
    clamps are discontinuous at |x| = 1 by 0.125 (§2.2). Drop `+a`, clamp at ±0.944, DC-block
    always.
22. 🔑 **Drift's tap velocity can exceed 1 sample/sample** at high Mod Rate × Time and read past
    the write head (§4.2). Needs both the per-sample clamp and the block-rate depth auto-scale.
23. **`setCharacter` has no upper clamp** (`DelayEngine.h:83`) — OOB read the moment a Character
    tuple table exists (§4.1).
24. **The in-loop `0.98` feedback cap (`DelayEngine.h:193`) plateaus the top 18 % of the Fdbk
    knob** — a shipped law-5 violation; do not build the Bucket makeup on top of the false
    "×1.2 reaches the loop" assumption (§3.2).
25. **Tape's Fdbk law flips with Character parity** (small-signal loop gain 1.20 vs 2.24) —
    pre-existing debt; log it, and do not use Tape as the calibration reference (§5.4).
26. **Diffuse reads `modDepthTgt` un-smoothed** (`DelayEngine.h:311`) — zipper on the smear
    crossfade (§4.0).
27. **Host automation lanes on `SYN_DLY_TYPE` remap when the list grows** — APVTS session state
    survives, automation does not (§3.5). Announce it; it cannot be fixed.
28. **`DelayEngine.h`'s own header comment (:6-7, :19-20) still advertises companding** that
    fb308 removed — fix while you are in there, or the next reader re-learns this the hard way.

---

## 13. §Hard-rule compliance checklist (laws 1-10, walked)

1. **Bus reality (−26 dBFS program; `kVoiceToFxPad = 0.5` at `PluginProcessor.cpp:6300`):** the
   corrected compander is reference-free *and* exactly invertible, so its round trip is
   level-independent — immune (§3.1, §5.3). **[AUDIT]** the original text credited the device
   with a "duck −40 dB floor"; that floor is **MoogDelay's** (`MoogDelay.h:480`). The *device*
   duck is `1/(1+14a·env)` (`DelayEngine.h:218`) — a smooth ratio with **no threshold at all**,
   which is the better answer and needs no recalibration. After the corrections there is **no
   absolute dBFS threshold anywhere in the ported path**. Drive rows are loop-side and
   level-independent via the tanh-normalized + makeup pairing (§3.2). ✅
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
6. **Nothing free-runs + loop gain law:** full ledger §5.4 — max stable loop gain **1.254·H
   (Digital/Bucket)**, ≈2.1 (Diffuse), all softClip-bounded (**[AUDIT]** the "1.2 everywhere"
   figure was wrong: the loop coefficient is capped at 0.98 and each type's makeup/saturator
   multiplies it). Chaos leaked; Pump is input-gated; Drift's tap velocity clamped (§4.2);
   Bucket's DC blocker always on so the type is silent on silence (§2.2); freeze parked
   pending Max. ✅
7. **No clicks:** every new path glided (time repitch glide, Character fade, S&H micro-
   glide, pitch dual-tap, compander is gain-continuous); click-floor probes specified. ✅
8. **CPU:** block-rate coefficients, no oversampling verdict with reasoning (§5.5),
   one-type-at-a-time, budget table (§10). ✅
9. **Audible ⇒ visible + dramatic:** three per-type viz layers riding the existing
   certified timeline + bloom push; idle-dim/playing-bright preserved (§7). ✅
10. **Recycle first:** the entire plan is a recycle — MoogDelay.h algorithms into
    DelayEngine.h seams, existing chassis, existing viz, existing preset menu; inventory
    §15. Plus the four dead `compEnv/expEnv` members + `compCoef` (§5.3) and the dead
    `SYN_DLY_WOW` param (§4.1) — **zero new state, zero new params**. ✅

### 13b. Rack-wide laws A-D — walked (added by the audit)

- **A · ZERO LOOKAHEAD anywhere in the rack.** ✅ Verified: `setLatencySamples` appears
  **nowhere** in `PluginProcessor.cpp`, so the plugin reports zero latency and the fb305/fb338
  exclusion sums stay sample-aligned. Nothing in this port adds latency — Bucket's filters and
  compander are one-pole/zero-delay, Drift only moves a read pointer, Shift is a second read
  pointer on the same buffer. **No lookahead limiting, no linear-phase option, ever.**
  (This also independently rules out the oversampling the §5.5 verdict already rejects, since
  polyphase up/down-sampling would report latency.)
- **B · No runtime parameter creation.** ✅ The port adds **zero** params; the only APVTS delta
  is the length of one `StringArray` at construction time (`PluginProcessor.cpp:3448-3450`).
  No slot pool is needed because no device list grows.
- **C · Choice cardinality is fixed at birth.** ⚠️ **The plan originally violated this** with
  "append new types at the END"; rewritten in §3.5 — size `SYN_DLY_TYPE` for its **final**
  roster in this one change, with unbuilt entries present-but-disabled, and accept/announce the
  one-time automation-lane remap. `SYN_DLY_CHARACTER` stays choice(8) forever.
- **D · Every send bus joins ALL THREE exclusion sums.** ✅ No new bus here (the delay reuses
  `delaySendBuf_`). The three live sites in this tree are
  `PluginProcessor.cpp:7159` (reverb), **`:7326` (distortion)** and `:7358` (delay) — the plan's
  §8 cited ":7325-7333" for the distortion twin, which brackets the right line. Re-check all
  three if a *fourth* device is ever added.

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
| 20-division sync resolve | `PluginProcessor.cpp:7217-7239` | unchanged |
| fbMk per-type makeup slot | `DelayEngine.h:198` | Bucket's `1.28·tanh(d)/d` (§3.2 corrected) |
| **Dead compander state** `compEnvL/R`, `expEnvL/R`, `compCoef` | `DelayEngine.h:76, 135, 358` | the rebuilt compander's envelopes — zero new state (§5.3) |
| **Dead param** `SYN_DLY_WOW` | `ParameterIDs.hpp:387`, `PluginProcessor.cpp:3480` | Bucket's Worn Cart / Memory wobble — gives a permanently-zero param a job (§4.1) |
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
| Type write (self-sizing from `options.length`) | `index.html:8246` | the §3.5 edit list; pairs with `DEVS[].types` :7483 + the C++ StringArray :3448-3450 |
| SYNCDIV/TIME_R normalized writes (N=20) | `index.html:8236, 8270-8276, 8292` | untouched by the Type change (§3.5 [AUDIT]) |
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
  **✅ RE-FETCHED AND TEXT-EXTRACTED BY THE AUDIT.** Confirmed verbatim: `Delay = N/(2·f_cp)`;
  the 4096-stage/300 ms → 6826⅔ Hz → 3413⅓ Hz worked example; anti-alias/recon filter
  topology and the "⅓…½ of the sampling frequency, as low as 1.5 kHz" cutoff window;
  SNR ≈ 60 dB; insertion gain 0…+2 dB at LF falling to **−4…−6 dB at Nyquist**;
  NE570/571 ratio **2**, `τ = 10000·C_rect`, C = 0.22–1 µF; feedforward expander
  `f(x) = avg(|x|)·x` and feedback compressor `f(x) = x/avg(|f(x)|)` (reference-free — no
  threshold); `THD = 1.01^(N/1024) − 1`; the third-order fit `f(x) = x − a·x² − b·x³ + a`
  with **a = 1/8, b = 1/18** and clamps `1−a−b` / `−1−a+b`. The two traps in that last item
  (discontinuity + DC) are the audit's analysis of the paper, not the paper's claim (§2.2).
- Moogerfooger family history (MF-104/Z/SD/M/MSD chips, dates, LFO section, plugins) —
  https://en.wikipedia.org/wiki/Moogerfooger
  **✅ RE-FETCHED.** Confirmed: MF-104 = 1,000 units / 40 ms–0.8 s; MF-104Z "slightly longer
  than 1 second"; MF-104SD 1.4 s / 250 units; MF-104MSD 1.2 s / 560 units; MF-104M announced
  **June 2012**, **"uses 4 MN3008s"** (the plan said 2 — corrected in §2.1/§2.2), 6-waveshape
  LFO; line discontinued **28 Aug 2018**; plug-ins **2022**. Not confirmed: the "2005" dates
  for MF-104Z/SD.
- MF-104S plugin parameter reference (TIME ranges, Range octave law, LFO 6 waves
  0.05-50 Hz, Drive, CV set, Legacy/Analog/Modern tone, Loose/Strict, Spillover) —
  https://moogconnect.net/m/mfs/mf-104s.html
  **✅ RE-FETCHED — every §2.1 claim confirmed**, including SHORT ≈8-400 ms / LONG ≈16-800 ms,
  "halves or doubles the delay time **and pitch**", self-osc "above 8" (≈3 o'clock), the six
  LFO shapes Sine/Tri/Square/Ramp/Saw/S&H at 0.05-50 Hz, DRIVE + the off/green/yellow/red
  LEVEL light, OUTPUT/LINK inverse coupling, CV on TIME/FB/RATE/AMOUNT/MIX, and the settings
  menu (TYPE Echo/Ping-Pong · TONE Legacy/Analog/Modern · TIMING Strict/Loose · LFO polarity
  bipolar/unipolar · bypass Normal/Spillover · FEEDBACK MODE Legacy/Modern). One addition:
  **TIME SYNC caps at 800 ms.**
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

- u-he Colour Copy, Arturia MEMORY-BRIGADE, ValhallaDelay, Matriarch, Serum 2 — URLs above.
  ⚠️ **NOT re-verified by the audit** (web-search budget exhausted): the Colour Copy /
  MEMORY-BRIGADE / Valhalla / Matriarch / Serum 2 figures in §2.3 are carried forward from the
  original research pass. Treat them as design colour, not as citable specification.

---

*Written 2026-08-14. Adversarially audited the same day: every repo line anchor re-read against
the tree, three primary web sources re-fetched, and the corrections marked **[AUDIT]** inline.*

**What the audit changed that a builder must not miss:** the missing `dpend > 3` clamp at
`PluginProcessor.cpp:7202` (§5.1 seam 0) · the compander expander formula (§3.1) and its
in-loop placement (§1.1) · the `fbMk` law, which as written made Bucket unable to self-oscillate
(§3.2) · the grit polynomial's DC constant and discontinuous clamps (§2.2) · Drift's missing
tap-velocity clamp (§4.2) · the append-only Type rule, which violated law C (§3.5) · and the
`PluginProcessor.cpp` line anchors in §1.3, which were uniformly 3-6 lines high.

*Builder-sufficient after these corrections. Anything still marked ⚠️ UNVERIFIED needs a
source before it is quoted.*
