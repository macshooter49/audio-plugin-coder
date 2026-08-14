# Terrain Instrument — Filter Build Bible

**v1 — research complete. The single authoritative spec for the 4th FX device.**
Written after fb345 (Phase G distortion certification closed). Every number below is either measured
on this machine, read out of a cited manual/paper, or read out of this repo with `file:line`.
No DSP written yet. Companion to `DISTORTION-BUILD-BIBLE.md` / `REVERB-BUILD-BIBLE.md` —
same structure, same laws, same chassis.

> ### 🔴 v1.1 — ADVERSARIAL AUDIT PASS (every C++ `file:line` in v1 was re-read against the tree)
> v1 shipped unaudited. These are the substantive corrections; each is marked inline where it lives.
> **Nine claims were factually wrong:**
> 1. `excite()` fires **only** for `Type::KARPLUS` — the "Pluck" character silently would not pluck (§0, §5.3).
> 2. The fb305 exclusion has **THREE** sites / six lines, not two — the **delay** block at `:7358/:7360` was missed (§3, §9).
> 3. `needsOversampling()`'s membership list was wrong (Scream chars aren't in it; XPD/Polivoks/Germanium/French are) (§0).
> 4. The Comb **Damp** citation `:1265` is `CombReverb`, not `COMB_DAMP` (`:1764`) — and it is a 3-place edit, not 5 lines (§4.6).
> 5. The free-run gate release coefficient is **60 ms** (applied squared), not "400 ms" (§6.5).
> 6. The fb304 sync list is **20 entries with `Free` at index 0** — a 19-entry clone is off by one (§2, §5.2).
> 7. Cutoff tops out at **20 kHz** and the clamp ceiling is `min(20 000, 0.45·fs_eff)`, not bare `0.45·fs_eff` (§2, §5).
> 8. The formant bank is **4 resonators** (F1..F4, Csound bass-singer table), not 3 (§4, §13).
> 9. Serum 2's Filter **FX module already has keytrack** — the "only we can" framing was false (§1.4, §5.3).
> **Plus four design defects:** the "internal latency compensation is a solved precedent" claim is
> **wrong and inherits a live comb from the shipped Distortion** (§6.3 MUST-RESOLVE) · Ring's
> "Shift Up / Shift Down" are the same sound (law 5 cut, §4) and Bode's Cutoff is a **bipolar shift,
> not a carrier** (which broke preset 12) · Type/Character choice sizes had zero headroom against
> law C while §16 Q5 proposes a 10th type (§7) · the DJ type cannot live in one `FilterSlot` (§4.9).
> **Verified OK:** every other `TerrainFilters.h` / `SynthVoice.h` / `DistortionEngine.h` /
> `DelayEngine.h` / `PluginProcessor.cpp` / `ParameterIDs.hpp` line reference in v1, the 94-type
> enum and `kNumTypes` at `:138`, `setType()` really does call `reset()` (`:1426-1431` — the fade
> law stands), all nine curated Types map to real in-tree enum values, the ±6 st `kSpreadSemis`
> math, the `+14 dB` bus arithmetic, and the FabFilter Volcano 3 / Cytomic The Drop / Mu-Tron III
> external claims (re-fetched from source). **Unverifiable at audit** (marked inline, not deleted):
> the Vox wah 450–1600 Hz / +18 dB figures and the 10 ms/500 ms Mu-Tron detector constants.

> ⚠️ **`index.html` line numbers drift with every UI build — re-grep the symbol, don't trust the
> number.** C++ refs (`TerrainFilters.h`, `DistortionEngine.h`, `DelayEngine.h`, `SynthVoice.h`,
> `PluginProcessor.cpp`) have been stable and were read directly for this doc.

---

## 0. The scope decision

**ONE device, named `Filter`.** The 4th flagship FX device in the rack, after Reverb, Delay,
Distortion.

### 🔑 0.0 THE BOUNDARY vs the synth FILTER panel *(added by the 2026-08-14 cross-bible sweep)*

Terrain will have **two filter surfaces** and a user must never have to ask which one to reach for.
`EQUALIZER-BUILD-BIBLE.md` §0.1 draws exactly this line for the two EQ surfaces; this is its twin,
and it was missing.

| | The synth FILTER panel (ships today) | The Filter DEVICE (this bible) |
|---|---|---|
| Where | The synth page, filter 1 / filter 2 tabs | The FX rack, a device card |
| Params | `SYN_FILTER1_*` / `SYN_FILTER2_*` (+ `SYN_FILTER_ROUTING`, per-filter Mix) | `SYN_FLT_*` — 3 heroes + Mix + back-8 (§7) |
| Instancing | **PER VOICE.** Two `FilterSlot`s live inside `SynthVoice`; every held note owns its own pair | **ONE stereo instance** on the FX bus, shared by everything routed to it |
| Position | **Pre-FX, inside the voice** — the house law is *engine → FILTER → effects*, and every send taps POST-filter | **Post-mix, on the bus**, at a position in the chain |
| What drives it | The voice's own envelopes, per-voice key-track, the mod matrix, per-voice unison detune | An **env follower on the bus** (§5.1), a **synced LFO** (§5.2), and key-track from the last note (§5.3) |
| The sound it makes | Each note filtered independently — a chord's notes each sweep on their own envelope | The **whole mix** filtered as one — a chord ducks and opens together. This is the auto-wah / DJ-filter / dub-sweep sound the synth filters structurally *cannot* make |

**The one-sentence law: per-voice filtering is the synth panel's job; whole-bus filtering that MOVES
with the program is the device's job.** They are not redundant — they are the two halves a
subtractive instrument needs, and Serum ships both for the same reason.

**Consequences for this build:**
* The device must **not** duplicate the synth panel's routing/series-parallel/two-filter grammar.
  One `FilterSlot`, one position in the chain — stack a second Filter device if you want two (that
  is what the chain epic is for, `FX-CHAIN-BIBLE.md` §3).
* Its **9 curated Types are a curated subset of the same 94-type enum** the synth panel reaches
  (`TerrainFilters.h:100-137`). That is deliberate reuse, not a second engine — but the device's
  Type list is sized and named for the *rack*, and it never tries to expose all 94.
* Where the two surfaces share a word (`Cutoff`, `Res`, `Drive`), the meaning must be identical —
  Tier-3 shared vocabulary (`FX-CHAIN-BIBLE.md` §7.2).
* ⚠️ **Type name check:** this device's Type `Phaser` sits in a rack that also ships a **Phaser
  device**, and Type `Comb`/`Ring` overlap nothing today. `Phaser`-the-filter-type is one biquad
  allpass chain used as a *static* coloured filter; `Phaser`-the-device is 9 topologies with motion
  and feedback. That is a Tier-2 collision on the most visible label a card has. **Recommended:
  rename this Type `Notches`** (what it does: a stack of fixed notches you sweep with Cutoff) and
  leave `Phaser` to the device — flagged to Max rather than applied, because §4.0's roster is still
  open and the rename should land with whatever roster wins.

### The thesis: we already own the hardest 90 %

Terrain's synth filter system is **already built and certified**: `TerrainFilters.h` is 2,195 lines,
**94 filter types** (`enum class Type`, `TerrainFilters.h:100-137`, `kNumTypes = 94` at `:138`),
seven-plus DSP cores (Huovilainen ZDF ladder, TPT SVF, diode ladder, 303, comb/Karplus, formant
bank, phaser, ring/Bode, and more), all reachable through one façade class — `FilterSlot`
(`TerrainFilters.h:1386`) with a complete, stable API:

| API | Line | What it does |
|---|---|---|
| `prepare(fs)` / `reset()` | `:1389` / `:1397` | alloc + state clear (resets ALL cores) |
| `setType(Type)` | `:1426` | swap active core — **calls reset(), see §6.6 fade law** |
| `setPoles(0..3)` | `:1435` | ladder slope 6/12/18/24 dB, CPU-free (both taps computed) |
| `setSpread(0..1)` | `:1438` | L/R cutoff split, ±6 st at full (`kSpreadSemis`, `:1440`) |
| `setParams(cutHz, res01, drv01, fs)` | `:1444` | per-sample; coefficient recompute change-gated `:1447-1454` |
| `processStereo(l, r)` | `:1923` | one stereo sample in place — the slot is **stereo internally** (every core exists as an L/R pair) |
| `needsOversampling()` | `:2085` | ⚠️ **exact set (read `:2085-2099`)**: Ladder LP24/12/6/18 + HP24 + German + Germanium + French, `DIODE_LP`, `ACID_303`, `ACID_SCREAM`, `POLIVOKS`, `WAVESHAPER`, `RING_MOD`/`RING_X2`, `BODE_SHIFT`/`BODE_DOWN`, and the whole `XPD_HP6..XPD_LP1` range. **It does NOT include `MS20_LP`, `WASP`, `SCREAM_LP`, `SCREAM_BP`** — i.e. 4 of the 5 Scream characters are *not* flagged. (Harmless under our fixed 2×, §6.3, but do not quote the old "Ladder+Diode+Acid+Waveshaper+Ring+Bode" shorthand — it was wrong.) |
| `setMorph(0..1)` | `:2103` | SEM/OB-X LP→Notch→HP morph — "wired for when a morph knob exists" (it now does: §7 Var) |
| `excite(level)` | `:2108` | 🚨 **fires ONLY for `Type::KARPLUS`** (`if (type_ == Type::KARPLUS)`, `:2110`). It is a **no-op for `KARPLUS_BRIGHT`, `KARPLUS_MUTE` and every `COMB_*`** — so the §4.6 "Pluck" character (= `KARPLUS_BRIGHT`) will NOT pluck without a core edit. Extend the `if` to the three Karplus types; that is a real (1-line) engine change, not free reuse. |

So this device is **not** a filter-DSP project. It is a **hosting + motion project**: put one
`FilterSlot` at the FX bus position, calibrate its gain staging to the real bus (§6.2), and build
the three modulation drivers that turn a static filter into an *effect*:

1. **Env Follower** — the auto-wah. The star. (§5.1)
2. **Synced LFO** — 4 bars → 1/256, the house sync law. (§5.2)
3. **Key Track** — the rack *knows the notes*. FabFilter, Soundtoys and Cytomic cannot do this
   without a MIDI routing safari. ⚠️ **but it is NOT unique among synth-internal racks** — Serum 2's
   Filter FX module already ships a keytrack toggle on CUTOFF ("1 octave MIDI = 1 octave cutoff",
   `Design/SERUM2-FX-REFERENCE.md §2.8`). Our differentiator is the *continuous* 0–200 % amount, the
   20 ms glide, and what it unlocks on Comb/Ring — not the existence of the feature. Do not put
   "only we can do this" in marketing copy. (§5.3)

### Why one device and not "Filter + AutoWah + Phaser + …"

Same argument that won the Distortion scope decision (`DISTORTION-BUILD-BIBLE.md §0`):

| | |
|---|---|
| **Serum is the bar** | Serum 2 ships **one** Filter FX device — "operates identically to the per-voice synth filter … as a master effect" (Serum 2 User Guide p.170), one Type menu spanning ladder/SVF/comb/formant/ring/phaser-ish types, 8 controls. One device. |
| **Our chassis is built for it** | 2 dropdowns + 8 back knobs + 3 front heroes + Mix (fb275). Ladder-vs-Comb is a smaller engine gap than Hall-FDN-vs-Convolution, which already ships behind one Type menu. |
| **No-doubles** | A separate Phaser device would collide with the Phaser types already in the 94-type enum and force a duplicate Cutoff/Res pair. |
| **The core is one object** | All 94 types live behind ONE `FilterSlot` (+ one parked SVF that only the DJ type instantiates — §4.9). One device ≈ one slot = the cheapest possible CPU story (§11). |

**What is out of scope here:** EQ (BellEQ/tilt types stay synth-side; a future Equalizer device owns
them), Bitcrush/Waveshaper types (`Type::BIT_CRUSH`, `Type::WAVESHAPER` — the Distortion device owns
that ground; shipping them here violates no-doubles), and the reverb-ish types
(`REVERB_FILT/DARK/METAL` — the Reverb device owns that ground). The 94-type synth menu keeps them
all; the **device** menu is curated to 9 (§4) per law 5 — a device dropdown of 30 lookalikes is a fail.

---

## 1. History and circuits — the lineage that defined the filter-as-effect

A filter inside a synth voice is a tone control. A filter **as an effect** is a *performance
instrument* — its whole history is the history of making the cutoff MOVE.

### 1.1 The wah pedal (1966) — cutoff on an ankle

The first filter effect. Invented November 1966 by Lester Kushner and Brad Plunkett at Warwick
Electronics/Thomas Organ (repackaging the Vox Super Beatle's mid-voicing circuit), sold from 1967
as the **Vox Clyde McCoy** and, for the US, the **Cry Baby**. Circuit: a single-transistor active
**inductor-based resonant band-pass**, rocker pot sweeping the peak from roughly **450 Hz to
1.6 kHz**, peak boost about **+18 dB**, fixed Q. ⚠️ **UNVERIFIED at audit time** — electrosmash.com
did not resolve and geofex.com could not be fetched; the 450/1600 Hz and +18 dB figures come from the
original researcher's notes only. Treat as approximate colour, not as a spec to calibrate against.
The lesson that survives into our device: *a resonant peak sweeping over ~2
octaves of midrange is intrinsically vocal* — it tracks the F1/F2 region of human vowels. That is
why a wah "talks."

### 1.2 Mu-Tron III (1972) — the envelope closes the loop

Mike Beigel's Musitronics **Mu-Tron III** (1972). ✅ **Verified** (Wikipedia, fetched at audit):
designed by Mike Beigel "at the instigation of Guild engineer Aaron Newman"; "the state variable
filter in the Mu-tron III allowed for low-pass, bandpass, and high-pass filter response"; "used
proprietary opto-isolators to control the filter, which was novel for the time". ⚠️ "**the world's
first envelope-controlled filter**" is Musitronics' own marketing line, not an independently
verified first — keep it in quotes or drop it. Controls: **Sensitivity** (detector gain), **Peak**
(resonance), **Range** (low/high), and the iconic **Up/Down drive switch** (sweep opens or closes
with level) — *control names unverified at audit; the up/down sweep is confirmed*. Detector
constants attack ≈ **10 ms**, decay ≈ **500 ms** are **UNVERIFIED** (geofex ECF-tech unreachable) —
our §5.1 defaults come from the in-tree `DelayEngine.h:133-134` (4 ms / 180 ms), which *is*
measured, so nothing calibrates off these numbers. Stevie Wonder's clav, Bootsy's bass, Jerry Garcia's leads — the auto-wah canon.
Our §5.1 env follower is a direct, calibrated descendant, and the front **Follow** knob is bipolar
precisely because of that Up/Down switch.

### 1.3 The synth filters (1965–1982) — the circuits our 94 types already model

* **Moog transistor ladder** (1965 patent): four cascaded one-pole RC sections inside a global
  feedback loop; resonance costs passband gain (the famous bass sag), transistor `tanh` limiting
  gives the compressing, creamy drive. In-repo: `LadderLP24` — Huovilainen's corrected ZDF form
  with per-stage tanh, tuning polynomial `fcr` and resonance correction `acr`
  (`TerrainFilters.h:140-151`), plus the fb-era bass-restore `driveComp = 1 + 0.5k` (`:163`).
* **Roland TB-303 diode ladder** (1982): diode-buffered 18 dB-ish ladder, resonance character glued
  to cutoff, the acid squelch. In-repo: `Acid303` (`:413`, "self-osc near k = 17") and `DiodeLP`
  (`:524`).
* **Korg MS-20 Sallen-Key** (1978): 2-pole Sallen-Key with a **diode clipper inside the resonance
  loop** — resonance doesn't just ring, it *distorts*; the scream. Rev-1 hard clipper vs rev-2
  LM13700 OTA soft clipper is exactly the split Cytomic's The Drop ships as MS1/MS2. In-repo:
  `MS20_LP`, `WASP`, `POLIVOKS`, `SCREAM_LP/BP` voicings.
* **Oberheim SEM 12 dB multimode** (1974): state-variable with the continuous **LP→Notch→HP morph**
  strip. In-repo: `SvfMultimode` with `Output::SEM` + `morph` (`:317-363`), Bencina/Wishnick-style
  TPT/trapezoidal core that stays stable under audio-rate cutoff modulation (`:315`).
* **Formant/vocoder bank**: Roland VP-330 (1979) analyzes voice through band-pass banks at
  200 Hz–6 kHz; three resonators are enough for a recognizable vowel. In-repo: `FormantBank`
  (`:768`) with `setVowel(v, shift, qScale)` and `setMorph` — A/E/I/O/U + morph + growl voicings.
* **Comb / Karplus** (1983): a tuned delay in feedback = harmonic spike series = a *pitched*
  filter. In-repo: `CombCore` (`:606`), modes Plus/Minus/Shimmer/Karplus + wide/octave/fifth/damp
  voicings, with a real note-on `excite()` pluck hook (`:2107`).
* **Phaser** (Small Stone / Phase 90, mid-70s): cascaded all-passes create N/2 movable notches;
  amplitude-flat until mixed with dry. In-repo: `PhaserCore` 4–16 stages (`:891`), `APDiffuser`.
* **Bode/ring** (1964 Bode frequency shifter; ring mod much older): multiply by a carrier —
  inharmonic sidebands at `f ± fc`; the Bode variant suppresses one sideband (SSB via Hilbert
  quadrature). In-repo: `RingMod` (`:929`), `BodeShifter` with 4+4 all-pass Hilbert (`:1127`).

### 1.4 The modern FX-filter genre — what the greats ship

* **Serum 2 "Filter" FX device** (the direct competitor): TYPE menu (the full synth filter
  taxonomy — MG ladders 6/12/18/24 + Fat, SVF Low/High/Band/Peak/Notch, dual "Multi" SVFs,
  morphing "Flange" combs/phasers with in-loop LP/HP, formants I/II/III, ring mod ×1/×2,
  sample-hold, French LP with "BOEUF" second resonance, German ZDF LP, Scream LP/BP with a
  feedback-cutoff VAR, Wasp, DJ Mixer, Diffusor, MG Dirty with the "PAIN" knob, Comb 2, expander
  MM/BPF), then **CUTOFF · RES · DRIVE · VAR (per-type relabel) · PAN (L/R cutoff offset) · MIX ·
  LEVEL**, and a right-click "Clean Mode" on Drive (−24 dB pre / +24 dB post). ⚠️ **corrected:** the
  keytrack toggle is **on the FX module's own CUTOFF**, not only on "the synth-side twin" — "CUTOFF
  (keytrack toggle; 1 octave MIDI = 1 octave cutoff)" per the in-tree
  `Design/SERUM2-FX-REFERENCE.md §2.8`. This is why §5.3's framing was softened. Visualizer: §8.1.
* **FabFilter Volcano 3** — ✅ **verified at audit** (fabfilter.com/help/volcano/using/filtercontrols):
  4 filters, shapes "low/high pass, band pass, bell, low/high shelf, notch or all pass", slopes
  **6/12/24/48 dB/oct** (6 dB/oct not offered for band-pass or notch), and exactly **11 filter
  "styles"** — Classic, Smooth, Raw, Hard, Hollow, Extreme, Gentle, Tube, Metal, Easy Going, Clean
  — a Character dropdown in all but name; XLFO, EG, **envelope follower with Transient mode**,
  MIDI, XY pads; per-filter panning.
* **Cytomic The Drop** — ✅ **verified at audit** (cytomic.com/product/drop/): the analog-modeling
  quality bar, exactly **11 filter models** — MS1 (Korg MS20 rev 1), MS2 (rev 2), OSR (OSCar), JRP
  (Jupiter 8 / Juno 6), SHR (SH-101 / SH-09), PRD (Moog Prodigy), WSP (Wasp), PT1 (Prophet 5 rev 1),
  SMP (custom Cytomic), AMU (transposed SVF), KSM (variation on MS2) — dual HP+LP serial/parallel,
  two Taurus-modeled envelopes, env followers, audio-rate LFO, self-oscillation with a "**safe**"
  button, and **separate realtime/offline oversampling** ("recommended settings are x2 for realtime
  and x8 for offline render"). Simper's published papers
  (`SvfLinearTrapOptimised2.pdf`, `SkfLinearTrapOptimised2.pdf`, ADC-2020 "Circuit to Code") are
  the math our SVF core already stands on.
* **Soundtoys FilterFreak**: 20 Hz–20 kHz cutoff, self-oscillates with no input, mod sources =
  LFO / **tempo-synced Rhythm patterns** / envelope follower / triggered ADSR / S&H / random-step,
  plus analog-style saturation modes.
* **Arturia FX Collection** (explicit deep reference): **Filter MINI** — TAE Moog-ladder 24 dB;
  Cutoff/Emphasis/Drive; LFO (4 waves + S&H, DAW sync), 8-step sequencer, env follower with
  **Sensitivity/Attack/Decay** that can also modulate emphasis and LFO rate. **Filter SEM** —
  12 dB multimode with the continuous LP→Notch→HP(+BP) morph, added noise osc, 16-step
  sequencer-gated LFO/env. **Filter M12** — two 15-mode Oberheim Matrix-12 filters, series/parallel
  routing, three multi-segment loopable envelopes, mod matrix. The Arturia pattern to steal: *the
  filter is one knob-row; the whole rest of the panel is motion sources.* That ratio is correct and
  it is our §7 back-8 ratio.
* **Xfer DJMFilter** (free, ubiquitous): Pioneer DJM-style **one-knob bipolar filter** — noon =
  bit-flat bypass, left = resonant LP sweep, right = resonant HP sweep. The single most-performed
  filter gesture in dance music; our DJ type (§4.9).

---

## 2. ⚡ NO PLAYING SAFE — the extremity table

The fb-documented root cause stands for this device too: the FX bus program sits at **−26 dBFS
nominal** (measured; `kVoiceToFxPad = 0.5` applied at `PluginProcessor.cpp:6300-6301`, the −6 dB
pad `PluginProcessor.cpp:26` documents). Any drive/threshold/sensitivity range copied from
literature or hardware lands ~26 dB short and ships timid. Every row below is calibrated to the
real bus.

| Param | 100 % must be | Why that is "just past useful" |
|---|---|---|
| Cutoff | full 20 Hz → **20 kHz** sweep (`cutKnobToHz = 20·1000^t`, `TerrainFilters.h:52-55` — `t=1` is exactly 20 000 Hz), clamp = **`min(20 000, 0.45·fs_eff)`** (the in-tree law, `SynthVoice.h:4190`: `fmax = jmin(20000, 0.45·coefSr)` — NOT bare `0.45·fs`) | at 20 Hz an LP is silence, at 20 k an HP is silence — the knob's ends ARE program mutes; that's the DJ gesture, not a bug |
| Res | **self-oscillation** on every core that can (ladder k→4, acid k→17, scream loop ≥1) — envelope-gated per law 6 (§6.5) | a filter that can't whistle is a tone control; The Drop, FilterFreak, Volcano all self-osc; gating (not capping) is our compliance |
| Drive | **+38 dB total** above bus program (fixed +14 dB bus lift + 0..+24 dB knob law, §6.2), taper `t^0.8` | +14 dB only *reaches* hardware nominal from −26; the knob's +24 on top is the actual push; at 100 % the ladder compresses to a fuzz — destruction allowed |
| Follow (front) | **±100 % = ±7 octaves** of env-driven sweep (84 semis) | full-range Mutron slam; at −100 % a snare closes the filter to a thump — night-and-day both directions |
| Sense | at 100 %, detector gain +48 dB — the filter pins full-sweep on **−48 dBFS** program (noise tails, reverb wash) | quiet material must be able to drive the wah; calibrated so 50 % ≈ full musical sweep at −26 dBFS program (§5.1 table) |
| Attack | 0.2 ms → 300 ms log | 0.2 ms = per-cycle "tearing" on bass (an audible, intentional artifact); 300 ms = swell |
| Release | 20 ms → 2 s log | 20 ms = stutter-wah on 1/16ths; 2 s = one slow exhale per phrase |
| Rate | stepped synced list **4 bar → 1/256** (law 3). ⚠️ the fb304 delay list is **20 entries**, `"Free"` at **index 0** then the 19 divisions (`PluginProcessor.cpp:3458-3459`) — clone it **whole, including Free**, or the "index-aligned" claim is false | 1/256 at 174 BPM = 172 Hz = audio-rate AM-like sideband growl — deliberately past "useful" |
| Sweep | 0 → ±5 octaves (60 semis) LFO depth, bipolar | ±5 oct traverses the whole keyboard span of cutoffs every cycle |
| Track | 0 → **200 %** (0.5 = 100 % = 1 oct/oct) | 200 % over-tracking splays the keyboard — high notes scream open, low notes seal shut; 100 % is the "playable comb" sweet spot |
| Var | per-type (§7) — each relabel reaches its own destruction (Morph full HP, Bite = scream, Blend full AM, Spin 180°) | |
| Mix | 100 % = FULLY WET, dry residual < −60 dB (house law 4, verified like fb-delay) | |

**Taper laws** (where the drama lives, per the Distortion §2.5 finding): Cutoff is already
exponential by construction (`20·1000^t` = 10 semis per 8.3 % of knob — perceptually linear).
Drive uses the house `t^0.8`. Attack/Release are log (`t² ` mapped into the ms range works;
state the exact map in §7). Sense is linear-in-dB. Follow/Sweep are linear in semis (motion depth
is perceptually linear in pitch space).

---

## 3. Repo recon — exactly what exists and what the device reuses

Verified by reading, not assumed:

* **The 94-type enum** — `TerrainFilters.h:100-137`. Frozen append-only after `NONE = 27` (fb165
  law in the header comment: saved states hold raw indices).
* **Gain/mapping helpers** — `cutKnobToHz` (`:52-55`), `driveLinear = 10^(24t/20)` i.e. 0..+24 dB
  (`:58-61`), `driveMakeup = driveLin^-0.5` (`:62-65`). The FX device reuses all three (§6.2).
* **Per-sample modulation grammar** — the synth voice already does everything §5 needs, in
  **semitone space**: `cutSemis = baseCutSemis + fMod·96 + lfoSemis + envCutSm + driftSemis +
  ktSm + velSm·72` (`SynthVoice.h:4195`), with the fb204 **2.5 ms filter-lane glide** on every
  block-pushed term (`SynthVoice.h:4113-4130`). Key track is `filterKeytrack · (note − 60)` semis
  (`SynthVoice.h:4088-4090`). The FX engine copies this summing law verbatim.
* **Oversampling contract** — the cores declare it (`needsOversampling()`, `:2085-2099`; ladder
  "2x default / 4x HQ" `:151`; Acid "4×, self-osc near k=17" `:411`; SVF "no oversampling needed
  unless DRV is hot" `:315`), and the voice honors it by doubling the coefficient sample rate
  (`coefSr = oversample ? sr·2 : sr`, `SynthVoice.h:4080-4084`). The FX device supplies the 2×
  wrapper (§6.3) — **reuse the Distortion halfband**, `DistortionEngine.h:74-97` (kT = 129 taps,
  centre 64, 64-sample base-rate latency, internally compensated — the §4.4 latency fix precedent).
* **Env-follower idiom** — `DelayEngine.h:213-218`: one-pole asymmetric follower (`duckAtk` 4 ms /
  `duckRel` 180 ms, `:133-134`) driving `g = 1/(1 + 14·amt·env)`. Ours is the same structure with
  knobs on the time constants and a calibrated dB mapping (§5.1).
* **Free-run gate** — `DistortionEngine.h:161-163`: input-envelope gate, 2 ms attack / ~0.4 s
  squared release, "scream dies WITH the note" (fb325/fb345 certified). Reused verbatim for
  self-osc gating (§6.5).
* **Deferred char-fade** — `dnDip_` 2 ms fade-down / re-seat / recover (`DistortionEngine.h:156`,
  fb345). Reused for Type/Character switches because `setType()` hard-resets (§6.6).
* **Live filter viz grammar** — the synth filter analyzer already draws **all 94 response curves**
  and glides a live post-mod cutoff/res node (fb163: `getFilterLive` native + 0.35 log-space glide;
  `index.html:25049-25090`, re-grep). The card viz reuses the curve math and the fb342/fb343 halo
  + write-gating laws (§8).
* **FX rack chassis** — `CORES` map keys `'reverb' | 'delay' | 'saturate'`
  (`index.html:7480-7490`, re-grep); device descriptors carry `pwrP/tp/rp` param IDs. New core key:
  **`'flt'`** (NOT `'filter'` — the synth page already owns a `.device.filter` CSS scope,
  `index.html:16469`; namespace-or-collide law from fx-rack v7).
* **Param grammar precedent** — `SYN_DST_*` block (`ParameterIDs.hpp:406-431`): Type/Character
  choices, front floats, back P1–P8 relabelled per family, SRC_A..NOISE routing bools, POWER
  default OFF, family pill. We clone this grammar as `SYN_FLT_*` (no collision — grepped; the synth
  filters use `SYN_FILTER1_/SYN_FILTER2_`).
* **The fb305/fb338 landmine** — every send bus must join **EVERY** main-send exclusion.
  🚨 **There are THREE sites, SIX lines** (verified by `grep -n "EVERY send bus joins EVERY
  main-send exclusion" PluginProcessor.cpp`), not two:
  **reverb** `:7159` / `:7161` · **distortion** `:7326` / `:7328` · **delay** `:7358` / `:7360`
  (all three are the same `(rvbSend + dlySend + dstSend) * outputGain * kVoiceToFxPad` sum).
  A 4th bus (`fltSend`) must be added to all **six** lines, **plus** the new device's own
  main-send branch needs a **fourth site** written with all four buses in it. Miss the delay site
  and the bug is silent on reverb/distortion tests. Documented defusal precedent: fb338.
* **FX order** — `SYN_FX_ORDER` is a 6-way permutation of 3 devices
  (`PluginProcessor.cpp:3488, :5860`). A 4th device makes it 24-way — see §16 (open question).
* **Karplus pluck** — `FilterSlot::excite()` (`:2108`) is a note-on hook the synth barely uses;
  wired to the FX device's key listener it turns Comb/Karplus into a playable resonator (§4.6).
  ⚠️ it currently guards `type_ == Type::KARPLUS` only (`:2110`) — the "Pluck"/"Bright" characters
  need the guard widened to `KARPLUS_BRIGHT`/`KARPLUS_MUTE`. Budget the engine edit.

---

## 4. §Types — the curated 9 (Type dropdown)

Law 5: each type must be night-and-day with a **measurable discriminator**. The curation rule:
one type per *physical mechanism*, never per response shape (response shapes are Characters).
All engine mappings verified against `FilterSlot::processStereo` (`TerrainFilters.h:1922-2010`).

The **Character** dropdown (2nd selector, fb275) re-voices each type from the existing enum —
zero new DSP for characters; they are curated views of the 94.

| # | Type | Mechanism | Engine mapping (existing `Type::` values) | Characters (dropdown 2) | 🔬 Discriminator (harness-measurable) |
|---|---|---|---|---|---|
| 1 | **Ladder** | 4× cascaded one-poles + global feedback, per-stage tanh — resonance costs bass, drive compresses | `LADDER_LP24/18/12/6`, `GERMAN_LP`, `LADDER_HP24` | Classic 24 · Wide 18 · Twelve · Six · German · Hi 24 | slope −24 dB/oct; passband drops with res (re-anchor law); H3-dominant drive; res=1 → pure sine self-osc |
| 2 | **Acid** | diode-ladder — res character glued to cutoff, squelch, k≈17 scream | `ACID_303`, `ACID_SCREAM`, `DIODE_LP`, `GERMANIUM_LP`, `FRENCH_LP` | Squelch · Scream · Diode · Germanium · French | res-peak Q *varies with cutoff* (measure Q at 200 Hz vs 2 kHz — ladder's doesn't); scream onset at drive+res corner |
| 3 | **Multi** | clean TPT SVF (Simper/trapezoidal), morphable response, no passband loss | `SVF_LP/HP/BP/NOTCH/PEAK`, `SVF_*24` (cascaded), `OBX_SVF` (SEM morph via `setMorph`) | Low 12 · Low 24 · High 12 · High 24 · Band · Notch · Peak · Morph | textbook magnitude match ±0.5 dB; **flat passband at any res**; Var sweeps LP→Notch→HP continuously (monotonic null-frequency track) |
| 4 | **Scream** | Sallen-Key / clipped-feedback — the resonance path itself distorts | `MS20_LP`, `WASP`, `POLIVOKS`, `SCREAM_LP`, `SCREAM_BP` | MS-20 · Wasp · Polivoks · Scream Low · Scream Band | THD **of the resonant peak** rises with res (ladder/SVF: falls or flat); feedback scream sustains past the res=self-osc line — env-gated |
| 5 | **Vowel** | **4-resonator** formant bank (F1..F4) — spectral envelope, not a cutoff. ⚠️ *not 3* — `FormantBank::VF[5][4]`, `TerrainFilters.h:770-790`, is a **bass-voice singer table (Csound Book)**, four formants per vowel | `FORMANT_A/E/I/O/U/MORPH/WIDE/GROWL` (Cutoff = vowel morph on Morph char) | Ah · Eh · Ee · Oh · Oo · Morph · Wide · Growl | **4** spectral peaks matching `VF[v][0..3]` (±3 %) — measure against the in-tree table, not a generic formant chart; vowel identity survives any input |
| 6 | **Comb** | tuned delay feedback — harmonic spike series, *pitched* | `COMB_PLUS/MINUS/SHIMMER`, `COMB_OCTAVE/FIFTH/DAMP`, `KARPLUS`, `KARPLUS_BRIGHT` | Plus · Minus · Shimmer · Octave · Fifth · Damp · Karplus · Pluck | spike spacing = f0 (Track 100 % ⇒ spikes land on the played note ±1 cent); Minus offsets by f0/2 (odd-only) |
| 7 | **Phaser** | all-pass cascade — notches in *phase*, flat in amplitude | `PHASER_4P/6P/8P/12P/16P`, `DIFFUSOR` | 4 · 6 · 8 · 12 · 16 · Diffuse | N/2 notches ≥30 dB (at internal mix); amplitude flat within 0.5 dB dry; group-delay signature |
| 8 | **Ring** | multiply by carrier — inharmonic sidebands `f ± fc`. 🚨 **Cutoff means two different things inside this one Type** (read `RingMod::setParams` `:935` vs `BodeShifter::setParams` `:1145`): on `RING_MOD`/`RING_X2`/`RADIO` Cutoff **is** the carrier in Hz (exponential, keytracks correctly); on `BODE_SHIFT`/`BODE_DOWN` the slot re-derives `cut01 = log(cutHz/20)/log(1000)` and turns it into a **bipolar linear shift, noon (cut01 = 0.5) = ZERO shift, ±2000 Hz at the ends**. Also `RES` on this Type is *not* resonance — it is the sine→diode-bridge blend (Ring) / shift-loop feedback (Bode). | `RING_MOD`, `RING_X2`, `BODE_SHIFT`, `BODE_DOWN`, `RADIO` | Ring · Dual · **Shift** · Radio (see ⚠️ below) | sidebands at `f ± fc` (not harmonics); Bode: one sideband suppressed — *rejection depth is **unverified**: the in-tree Hilbert is only 4+4 all-passes (`:1127`), so the >30 dB figure in §13 must be MEASURED before it is quoted* |
| 9 | **DJ** | one-knob bipolar LP⟷flat⟷HP, bit-flat at noon | `SVF_LP24` below center / `SVF_HP24` above (device-level bipolar map of Cutoff) | Clean · Color · Dub · Slam | **exact null at Cutoff = 50 %** (residual < −80 dB); res auto-rises toward the extremes (the DJM law) |

> 🚨 **Law-5 cut applied to the Ring roster (audit fb-filter):** the original draft listed **Shift Up**
> *and* **Shift Down** as two characters. They are **not night-and-day** — `BODE_DOWN` differs from
> `BODE_SHIFT` by exactly one line, `dirMul = −1.0f` (`TerrainFilters.h:1611-1616` vs `:1821-1826`), and
> the control it flips is *already bipolar*. "Shift Up at Cutoff 0.3" and "Shift Down at Cutoff 0.7"
> are the **same sound**. Ship **one "Shift" character** (`BODE_SHIFT`, bipolar Cutoff, noon = no
> shift) and let the knob choose direction. Inventing a second entry to fill the dropdown is the
> other half of law 5. Ring drops 5 → **4** characters.

**Cuts (and where their sounds live):** `BIT_CRUSH`, `WAVESHAPER` → the Distortion device (no-doubles).
`REVERB_FILT/2/DARK/METAL` → the Reverb device's ground. `TILT/LOW_EQ/HIGH_EQ/BAND_EQ/AIR/ADD_BASS`
→ reserved for a future Equalizer device. `SAMPHOLD/±`, `GRAIN_MASK`, `BODE` stays (in Ring),
`SAMPHOLD` is Distortion-family territory (Downsample twin — no-doubles). All remain available in
the synth filter menu; nothing is deleted.

**Per-type notes for the builder:**

* **Ladder (4.1)** — reference type, boots first. `setPoles` gives 24/18/12/6 free; Character maps
  poles + `GERMAN_LP`/`LADDER_HP24` type swaps. Var = **Slope**: continuous pole morph via
  `LadderPoleMix` (`TerrainFilters.h:251`) — a knob Serum doesn't have (theirs is 4 discrete menu
  entries).
* **Acid (4.2)** — Var = **Accent**: env-follower → *resonance* depth (the 303 accent mechanism);
  device-level math (`res_eff = res + accent·env·(1−res)`), no core edit.
* **Multi (4.3)** — Var = **Morph** via `setMorph` on the SEM path; for the 24 dB characters morph
  both cascaded stages. The "wired for when a morph knob exists" comment (`:2103`) is finally paid.
* **Scream (4.4)** — Var = **Bite** → `SvfMultimode::setDrive` (`:363`) on the MS-20/Wasp/Polivoks
  chars; scream-loop gain on Scream chars. Self-osc gate mandatory (§6.5).
* **Vowel (4.5)** — Var = **Shift** → `FormantBank::setVowel(v, shift, …)` (`:824`) formant shift
  ±5 st: child ↔ giant. On the Morph character, Cutoff = vowel morph (`setMorph`, `:837`) — the
  knob literally talks.
* **Comb (4.6)** — Var = **Damp** (in-loop brightness). ⚠️ **the draft cited `:1265` — that line is
  inside `CombReverb` (`struct CombReverb`, `:1244`), the core behind `REVERB_FILT_2`, which this
  bible explicitly cuts as Reverb-device ground.** The real targets are:
  (a) `case Type::COMB_DAMP` at **`:1764-1772`**, where `dampL_.damp` / `dampR_.damp` are
  **hard-coded `0.5f`** — that is the line the Var replaces;
  (b) `DampComb<>::process` at `:1236-1242` (`lpZ = (1−damp)·y + damp·lpZ`) — the law itself;
  (c) `CombCore` (`:606`) serves Plus/Minus/Shimmer/Karplus and has its **own** damping path.
  So "Damp across all 8 Comb characters" is an edit in **three** places, **not** "the one place a
  core gets a 5-line edit". Re-scope the build estimate accordingly.
  Note-on calls `excite()` on the Karplus chars — **after** widening the `:2110` type guard (§0).
* **Phaser (4.7)** — Var = **Spin**: L/R LFO phase offset 0..180° (device-level; quadrature phaser
  stereo). Phaser ignores Drive's color? No — drive feeds `PhaserCore.setParams` driveLin as today.
* **Ring (4.8)** — Var = **Blend**: RM → AM (device-level: `out = x·(1−b) + b·(x·carrier)` re-mixed
  with the dry leg so b=0.5 ≈ classic AM with carrier bleed). Cutoff = carrier freq on the Ring/Dual/
  Radio chars (as in Serum — `RingMod::setParams(carrierHz, …)`, `:935`). ⚠️ **On the Shift (Bode)
  char, Cutoff is a bipolar ±2000 Hz shift with noon = zero shift** (`:1145-1152`), so:
  (a) key-track in **semitone** space does **not** make a Bode shift track the note — a linear-Hz
  shifter is inharmonic by definition; **Track has no musical meaning on Shift** — grey it or map it
  to nothing and say so;
  (b) **preset 12 "Bell Machine" (Ring/Shift Up, Cut .5) is broken as written** — Cut .5 is
  *exactly zero shift*, i.e. bypass. Fixed in §10.
  Track 100 % = the **Ring/Dual** carrier tracks the note — those sidebands become *playable*.
* **DJ (4.9)** — Cutoff remap: `t<0.5 ⇒ LP, fc = 20·1000^(2t)` up to 20 kHz at noon;
  `t>0.5 ⇒ HP, fc = 20·1000^(2t−1)`; exact-flat bypass branch at `|t−0.5| < 0.004` (with 10 ms
  crossfade so the null engages click-free). Res auto-curve: `res_eff = res + curve·(2|t−0.5|)^1.5·(1−res)`
  (Var = **Curve**). Characters add drive coloration (Color/Slam) or LP-biased res voicing (Dub).
  🚨 **DJ breaks the "one slot" claim of §0** — one `FilterSlot` holds **one** `type_` at a time, and
  crossing noon means `setType(SVF_LP24) → setType(SVF_HP24)`, which **hard-resets** (`:1426-1431`).
  Applying the §6.6 fade-dip on every noon crossing puts an audible 2 ms hole in the middle of the
  single most-performed gesture in the device. **Fix: the DJ type instantiates a SECOND `FilterSlot`**
  (`djHp_`) permanently on `SVF_HP24`, main slot permanently on `SVF_LP24`, both always in circuit
  with the inactive leg parked at its transparent extreme (HP at 20 Hz / LP at 20 kHz). This is how
  real DJ filters are built (serial LP+HP, one always wide open), it removes the type swap entirely,
  and it costs one extra SVF (≈ 0.05 % — §11). Correct §0/§11 to "one slot **+ one parked SVF for DJ**".

---

## 5. The motion block — the device's actual new DSP

All three drivers output **semitones**, summed exactly like the voice does
(`SynthVoice.h:4195` grammar), then converted once and clamped:

```
cutSemis = hzToSemi( cutKnobToHz(cut01) )        // base — the front Cutoff knob
         + followSemis                           // §5.1 env follower (front Follow · back Sense/Attack/Release)
         + lfoSemis                              // §5.2 (back Rate/Sweep)
         + keySemis                              // §5.3 (back Track)
cutHz    = clamp( semiToHz(cutSemis), 20, min(20000, 0.45·fs_eff) )   // ← the in-tree law
```

⚠️ **The clamp ceiling is `min(20 000, 0.45·fs_eff)`, not bare `0.45·fs_eff`.** The voice does exactly
this (`SynthVoice.h:4190`: `fmax = jmin(20000.0f, 0.45f * coefSr)`). At the device's fixed 2×,
`0.45·fs_eff` = 43.2 kHz at 48 k — dropping the 20 kHz cap would let modulation drive the cutoff two
octaves above anything audible. It matters *doubly* here because **seven of the nine types re-derive
`cut01 = log(cutHz/20)/log(1000)` inside the slot** (Vowel `:1560`, Bode `:1147`, and every
`cut01`-based case): above 20 kHz `cut01` saturates at 1.0 and the top of the modulation range goes
**flat — a plateau, which is a law-5 failure**. Keep the cap and scale Follow/Sweep depth so the
useful excursion lands inside it.

Every term is glided (2.5 ms one-pole, the fb204 filter-lane law) before summing. `setParams` is
called **per sample** with the summed value — the change-gate (`:1447-1454`) self-disables while
modulating; §11 handles the CPU consequence.

### 5.1 Env Follower — the star (auto-wah)

**Detector.** Stereo peak rectify `rect = max(|L|,|R|)`, one-pole asymmetric:

```
env += (rect > env ? aA : aR) · (rect − env)
aA = 1 − e^(−1/(fs·tA))     tA = Attack knob:  0.2 ms → 300 ms, log taper, default 4 ms
aR = 1 − e^(−1/(fs·tR))     tR = Release knob: 20 ms → 2 s,    log taper, default 180 ms
```

(The `DelayEngine.h:213-218` idiom with the fixed 4 ms/180 ms constants promoted to knobs.
Defaults ARE those constants — certified musical since fb-delay.)

**Sensitivity (house-law calibration — the whole point).** Detector gain in dB, linear knob:

```
g_s = 10^((6 + 42·sense)/20)          // Sense 0 → +6 dB, 0.5 → +27 dB, 1 → +48 dB
lvl = env · g_s
sweep01 = lvl / (1 + lvl)             // soft knee — never plateaus, evolves 0→100 (law 5)
followSemis = Follow · 84 · sweep01   // Follow ∈ [−1, +1] front knob; 84 semis = 7 octaves
```

Calibration table (state it, verify it): sine program at the given RMS, Sense 0.5, Follow +100 %:

| Program | env (lin) | lvl | sweep01 | sweep |
|---|---|---|---|---|
| −36 dBFS | 0.016 | 0.35 | 0.26 | +1.8 oct |
| **−26 dBFS (bus nominal)** | 0.050 | 1.12 | **0.53** | **+3.7 oct** |
| −16 dBFS | 0.16 | 3.5 | 0.78 | +5.5 oct |

Half-sweep at bus nominal with headroom both ways = the Mutron center. All thresholds stated
relative to −26 dBFS per law 1.

**Direction.** Follow is **bipolar** (center detent 0): up = opens with level (classic), down =
closes (the Mu-Tron "Down" switch — reverse wah, ducky pads). No separate switch param.

**Punch pill (transient mode).** Volcano 3 precedent: `env_t = max(0, envFast − envSlow)` with a
1 ms / 50 ms pair replacing `env` — the follower becomes a transient detector; the filter *blips*
on attacks only. Front pill 2 (§7).

### 5.2 LFO — synced, one master clock

* **Rate** = stepped knob over the house synced list, **4 bar → 1/256**. ⚠️ **corrected:** the fb304
  delay list (`PluginProcessor.cpp:3458-3459`, read verbatim) is **20 entries with `"Free"` at index
  0**: `Free, 4 bar, 2 bar, 1 bar, 1/2, 1/2D, 1/2T, 1/4, 1/4D, 1/4T, 1/8, 1/8D, 1/8T, 1/16, 1/16D,
  1/16T, 1/32, 1/64, 1/128, 1/256`. A 19-entry list that starts at "4 bar" is **off by one** against
  it — "index-aligned" would be a lie and every saved Rate would read one division fast if the two
  lists were ever cross-read. **Clone all 20 including `Free`** (Free = the free-run Hz fallback the
  one-clock law already needs when the transport is stopped). Default **`1/4` = index 7**.
* **Sweep** = depth, 0 → ±60 semis (5 octaves), sine.
* 🔑 **ONE-CLOCK LAW (fb345):** derive phase from the host — `ph = fmod(ppqPosition /
  beatsPerDiv, 1)` at block start + in-block increment; never free-integrate a per-sample
  accumulator against a glided rate (phase accumulators integrate glide skew — the DIGITAL Spread
  bug class). Transport stopped ⇒ free-run at the equivalent Hz from the last BPM, phase-continuous.
* Waveform is sine only. Shapes belong to the synth's 10-LFO system + mod matrix; a device LFO
  that grows 6 waveforms is scope creep the mod matrix already covers. (Open question §16 if Max
  wants Tri/Saw/Square here.)
* Stereo: the **Spin** Var (Phaser) and the Wide pill (§7) offset R-channel LFO phase.

### 5.3 Key Track

External filter plugins cannot know the note. We can. (Scope honestly: Serum 2's Filter FX already
has a keytrack toggle — see §0 note. Ours is continuous 0–200 % + glided + wired to `excite()`.)

```
keySemis_target = Track · 2 · (note − 60)     // Track 0..1 → 0..200 %; 0.5 = 100 % = 1 oct/oct
keySemis += (keySemis_target − keySemis) · g20ms   // 20 ms glide — legato sweeps, no steps
```

* `note` = **latest note-on across all voices** (mono, last-note priority): one relaxed atomic
  `int lastNoteFx_` written where the processor already dispatches note-ons
  (`PluginProcessor.cpp:6008` region, next to `flowArp.noteOn`). Note-off does NOT clear it — the
  filter holds the last pitch (release tails keep their color; law: nothing turns off by itself).
* At Track 100 %, **Comb becomes a playable resonator** (spikes on the played note — target ±1 cent,
  to be *measured*, §13), **Ring/Dual become a playable sideband instrument** (carrier = Cutoff in
  Hz; **not** the Shift/Bode character, whose shift is linear-Hz — §4.8), and any res'd-up LP
  self-osc **plays the keyboard** (env-gated, §6.5). These three demos are the marketing reel.
* Karplus chars: note-on also fires `FilterSlot::excite(velocity)` (`:2108`) — a true pluck, **after**
  the `:2110` guard is widened past `Type::KARPLUS` (§0).
* Center C4 = MIDI 60, matching the synth's keytrack law (`SynthVoice.h:4088`).

---

## 6. DSP core — signal flow, gain staging, oversampling, stability

### 6.1 Signal flow (per sample, inside the device)

```
in L/R ──► bus lift (+14 dB fixed) ──► Drive (0..+24 dB, t^0.8) ──► [2× upsample]
     ──► FilterSlot::processStereo (per-sample setParams w/ summed cutSemis, res_eff, drv01)
     ──► [2× decimate] ──► post makeup (driveMakeup + type makeup, already inside the slot)
     ──► bus restore (−14 dB) ──► DC blocker (10 Hz, fs-aware) ──► equal-power Mix vs delayed dry
```

### 6.2 Gain staging — the house drive law applied

The synth's own drive law (`driveLinear` 0..+24 dB + `driveMakeup = g^-0.5`,
`TerrainFilters.h:58-65`) is correct **at voice level**. At the FX position the program is ~26 dB
lower, so we split the difference into an architecture:

* **Fixed +14 dB bus lift** on device input, **−14 dB restore** on output. At Drive 0 the pair
  nulls exactly (verify: residual < −90 dB, Multi type, res 0, cutoff 100 %). The lift moves
  −26 dBFS program to ≈ −12 dBFS at the cores — the level the tanh/diode stages were voiced for
  in the synth.
* **Drive knob** = the slot's own `drv01`, FX-tapered: `drv01 = t^0.8` (house law shape), i.e.
  0 → +24 dB into the core with the existing `driveMakeup` half-compensation. Total available push
  above bus program: **+38 dB** (§2 row). No literature range was copied; the +14 figure derives
  from the measured bus vs the voiced core level.
* `Auto` output compensation: **none** — the slot's per-type `postMakeup_` + `driveMakeup` already
  hold loudness within a few dB, and full normalization is the documented timidity culprit
  (`SYN_DST_AUTO` note, `ParameterIDs.hpp:429`).

### 6.3 Oversampling verdict

**The whole device runs at a fixed internal 2×** using the certified Distortion halfband pair
(`DistortionEngine.h:74-97`: 129-tap polyphase, −67 dB at 26 kHz, passband 0.00 dB at 20 kHz,
64-sample base-rate latency ≈ 1.3 ms). Factor the FIR into a small shared `Halfband2x` utility
(same coefficients, same fb342 mirrored-ring vectorization) rather than copy-pasting.

* Every core is `prepare`d and `setParams`'d at `fs' = 2·fs` — exactly the voice's `coefSr`
  doubling scheme (`SynthVoice.h:4080-4084`), so ZDF prewarp sees the doubled Nyquist.
* This clears the declared per-core budgets in one move: ladder "2× default" (`:151`), Acid "4×"
  (`:411` — 2× + its internal soft-clip is the accepted voice-parity budget; 4× reserved, §16),
  ring/Bode sideband folding halved.
* **Uniform 2× for ALL types** (even SVF/comb/phaser that don't need it) because: (a) latency is
  then **constant and type-independent** — switching Type never changes delay, so type fades stay
  clean; (b) the CPU cost of 2× on the cheap cores is noise for ONE instance (§11).
* 🔑 **Latency law — RACK LAW A (ZERO LOOKAHEAD / zero reported latency).** The device reports
  **zero** to the host. That is non-negotiable: the fb305 main-send exclusions subtract the routed
  dry **sample-aligned** (`leftChannel[i] − rtd[i]`, `PluginProcessor.cpp:7159` etc.), so any
  latency-reporting device makes the dry leak back phase-smeared. No lookahead, no linear-phase
  anything, anywhere in this device.
* 🚨 **MUST RESOLVE BEFORE BUILD — the internal 64-sample delay is NOT a solved precedent.** The
  draft said "identical to the shipped Distortion decision". Read what that decision actually is:
  `DistortionEngine.h:997-1002` aligns Mix by **delaying the dry** (`dryAligned = dr[(di −
  kOsLatency) & kRM]`), so at Mix 0 the engine returns `in[n−64]`, not `in[n]`. The host-side insert
  is `leftChannel[i] += e · (wl − sgL)` (`PluginProcessor.cpp:7337-7339`), which subtracts the
  **un-delayed** `sgL[n]`. At low Mix in main-send mode that evaluates to `in[n−64] − in[n]` — a
  64-sample comb (nulls every ≈750 Hz at 48 k), inaudible at Mix 0 or 100 but present in between.
  Copying the pattern blind copies the flaw. Pick one, explicitly, and write a §13 row for it:
  1. **(recommended) Delay the insert's reference too** — keep a 64-sample ring of `sg` in the block
     loop and subtract `sgDelayed[n−64]`; the device then contributes exactly 0 at Mix 0 and the
     whole-mix insert is coherent. The routed-dry `rtd` used by the exclusion must be read from the
     **same** delayed tap or the exclusion re-smears (law A). Costs one ring per bus.
  2. **1× (no oversampling) for the linear types** (SVF/Multi/Comb/Phaser/Vowel/DJ — none of them
     declare `needsOversampling()`), 2× only for Ladder/Acid/Scream/Ring. Zero latency in the common
     case, but latency becomes **type-dependent**, which breaks the constant-latency argument below
     and makes Type fades change delay. Only viable if paired with (1) for the 2× types.
  3. Ship 2× everywhere and **accept the comb**, with Mix locked to 100 % on main-send. Worst option;
     documented only so nobody rediscovers it as a "bug".
  ⚠️ **Also file this against the shipped Distortion device** — the same arithmetic applies there and
  it was never in the Phase G table. Run the null harness on DST main-send at Mix 0.5 before copying.

### 6.4 Stability + loop-gain ledger (law 6)

Account for every gain stage inside each feedback loop:

| Loop | Gain elements | Max stable condition |
|---|---|---|
| Ladder | k = 4·res·acr around 4 one-poles, per-stage tanh | tanh **bounds amplitude by construction**; k ≤ 4·acr; self-osc is the design point at res→1, DC-safe via §6.7 |
| Acid | diode loop, self-osc near k = 17 (`:411`) | internal soft-clip bounds it; "only fully tame when oversampled" (`:567`) — our fixed 2× satisfies the note |
| Scream | svf drive + feedback clipper / `fbScr` path | clipper bounds; res_eff gated (§6.5) so sustained scream requires input |
| Comb | feedback g = res01 mapped internally | g ≤ 0.98 by the core's own clamp; delay-length GLIDES (comb-click law) — never snap `setLen` |
| SVF | trapezoidal/TPT | unconditionally BIBO for res ∈ [0,1] even under audio-rate cutoff (Simper/Bencina analysis — cited §17) |

**Max stable loop gain statement:** no loop in the device exceeds unity *unbounded* gain; every
loop ≥ unity is amplitude-bounded by an in-loop nonlinearity AND envelope-gated (§6.5). That is
the full law-6 compliance sentence for the review.

### 6.5 Envelope-gated self-oscillation (nothing free-runs)

Reuse the fb325/fb345 gate verbatim. ⚠️ **Use the literal constants — the draft's "~400 ms squared
release" is the *observed behaviour*, not the coefficient, and a builder who writes
`exp(−1/(fs·0.4))` gets a gate 6.7× too slow.** From `DistortionEngine.h:161-163`:

```
envA_ = 1 − exp(−1/(fs · 0.002));   // 2 ms attack
envR_ = 1 − exp(−1/(fs · 0.060));   // 60 ms one-pole release  ← the actual number
// the gate is applied SQUARED (fb345 grid-leak law); 60 ms squared ⇒ the audible
// free-run gate closes in ~0.4 s — "scream dies WITH the note" (the header's own comment)
```

Law:

```
g = inEnv gate ∈ [0,1]
res_eff = res ≤ 0.90 ? res : 0.90 + (res − 0.90)·g
```

Below res 0.90 nothing changes (normal resonance rings and dies naturally). Above it, full
self-oscillation is available **while the note feeds the bus** and decays to the stable-ring
region within ~1 s of silence — a whistle dies with the note instead of droning forever.
(The Drop ships a "Safe" toggle; our gate is better: full scream when playing, silence when not,
no chicken switch.) Verify: §13 free-run row.

### 6.6 Type/Character switches — fade-swap-recover

`setType()` hard-resets all cores (`:1426-1431` — deliberate, stale-tail law). A bare call clicks.
Reuse the fb345 deferred-fade: output dips to 2 % in ~2 ms (`dnDip_` constant), the pending
type/character/poles/morph swap lands at the dip floor, state re-seats, output recovers over
~40 ms. Same idiom for the DJ null-crossing branch swap. Dropdown switches never cut audio (law 4).

### 6.7 Engineering musts (the small print that bites)

* **DC blocker** — 10 Hz, sample-rate aware, after the device (the in-tree `DCBlocker` hardcodes
  0.995 = 38 Hz at 48 k; the Distortion header documents the bug, `DistortionEngine.h:122-124` —
  use its fs-aware form). Asymmetric-drive ladder + ring mod both emit DC.
* **Grid-leak class (Phase G)** — any envelope-tracked bias/gate state must AC-couple or
  env-track, never latch: the self-osc gate uses the squared-release form that killed the DC-latch
  silence class in fb345.
* **Denormals** — `ScopedNoDenormals` in the block + slot `reset()` on power-off; comb/SVF states
  flush on bypass (reverb precedent).
* **Param glides** — every continuous param 10–30 ms one-pole (Cutoff/Res/Drive/Follow/Sense/Var);
  Attack/Release knobs glide their *coefficients*; Track glides in semis (§5.3); Rate steps
  crossfade phase-continuously (one-clock law). Choice params: fade-swap (§6.6).
* **Choice-param read law** — `(int)*rawParam(id)` = the index directly, never scaled
  (CLAUDE.md §4 — the fb50 class).
* **WebSliderRelay 4-point binding** for every new `SYN_FLT_*` param, or it silently no-ops
  (CLAUDE.md §4).
* **Mix at 100 % = fully wet** — dry residual < −60 dB, offline-verified (house law, fb-delay
  precedent).

---

## 7. §Chassis map — the 11 params on the fb275 chassis

**APVTS block: `SYN_FLT_*`** (no collision; grepped). Plus the standard `SYN_FLT_SRC_A..NOISE`
routing bools, `SYN_FLT_POWER` (bool, **default OFF**, gates routing — house law), and two pills.

### Front card (3 heroes + Mix) — pragmatic names, Title-case

| Knob | ID | Range → mapping | Taper | Glide | Default |
|---|---|---|---|---|---|
| **Cutoff** | `SYN_FLT_CUT` | 0..1 → 20 Hz..20 kHz (`cutKnobToHz`); DJ type: bipolar remap §4.9 | exp (built-in) | 10 ms | 0.62 (≈ 1.1 kHz) |
| **Res** | `SYN_FLT_RES` | 0..1 → per-core res; > 0.90 = gated self-osc (§6.5) | linear | 15 ms | 0.35 |
| **Follow** | `SYN_FLT_FOLLOW` | −1..+1 (center detent 0) → ±84 semis × sweep01 (§5.1) | linear in semis | 15 ms | +0.40 |
| **Mix** | `SYN_FLT_MIX` | 0..1 equal-power; 1.0 = FULLY WET | equal-power | 15 ms | 1.00 |

*Why Follow (not Drive) gets the third hero slot:* the mandate — env follower is the star; the
device's identity is a filter that MOVES. Drive is a back knob, exactly where Arturia puts it.

**Pills** (front, per the device-pill grammar): **Wide** (on = `setSpread(0.5)` → ±3 st L/R split +
90° R-channel LFO phase; glided) · **Punch** (env follower transient mode, §5.1). Plus the standard
Power pill. Default both OFF.

### Back panel — 2 dropdowns + 8 knobs (4×2)

🚨 **RACK LAW C — choice cardinality is fixed at birth (fb342, session law ①).** An
`AudioParameterChoice` list **cannot grow in place**; the host caches it. Sizing `SYN_FLT_TYPE` to
exactly 9 while **§16 Q5 of this same document proposes a 10th type** is a self-contradiction and a
guaranteed re-birth of the param. Size both dropdowns for the **final** roster on day one, with
disabled/hidden tail entries (the delay `SYNCDIV` precedent):

> 🔧 **[CROSS-BIBLE AUDIT 2026-08-14] CHASSIS CORRECTION — `Type` is the HEADER PILL, not back-d1.**
> Verified in the shipped tree: on Reverb, Delay **and** Distortion, `*_TYPE` renders in the header
> `.fxr-type` `<select>` on the card centerline (`index.html` `DEVS[].tp` +
> `Design/fx-back-panel-mockup.html`); the two **back** dropdowns are `Character` + a second
> selector (`Mod Mode` / `Sync` / `Quality`). Spending back-d1 on `Type` duplicates the header pill
> — the most visible label the card has — and silently throws away a back dropdown this device is
> entitled to. Move `Type` to the header, slide `Character` to back-d1, and back-d2 is free.
> Full ruling (incl. that the honest knob count is **12** = 3 heroes + Mix + 8 back, not the "11"
> four bibles reconstructed four different ways): `FX-CHAIN-BIBLE.md` §7.1.

**Dropdown 1 — Type** (`SYN_FLT_TYPE`, **choice 12** — 9 live + 3 reserved, disabled in the UI):
Ladder · Acid · Multi · Scream · Vowel · Comb · Phaser · Ring · DJ · *(Reserved 1..3)*.
Default Ladder (index 0). The three spares cover §16 Q5 (Sample Hold), a future Formant-2, and one
free slot — cheaper than ever re-versioning the param.
**Dropdown 2 — Character** (`SYN_FLT_CHARACTER`, **choice 12**, per-type roster §4; unused tail
entries hidden per type). Today's longest roster is 8 (Multi/Vowel/Comb); 8 with **zero** headroom is
the same trap one level down — a single added voicing on any type re-births the param. Default =
first entry.

| Slot | Knob | ID | Range → mapping | Taper | Default |
|---|---|---|---|---|---|
| P1 | **Drive** | `SYN_FLT_DRIVE` | 0..1 → `drv01 = t^0.8` → 0..+24 dB into the core (over the fixed +14 lift, §6.2) | t^0.8 | 0 |
| P2 | **Var** *(relabelled per type)* | `SYN_FLT_VAR` | Ladder **Slope** (pole morph) · Acid **Accent** (env→res) · Multi **Morph** (LP→N→HP) · Scream **Bite** (svf drive/fb) · Vowel **Shift** (±5 st formant) · Comb **Damp** · Phaser **Spin** (0..180°) · Ring **Blend** (RM→AM) · DJ **Curve** (res law) | linear | per-type (Morph 0, Spin 0.25, else 0) |
| P3 | **Track** | `SYN_FLT_TRACK` | 0..1 → 0..200 % keytrack (0.5 = 1 oct/oct), §5.3 | linear | 0 |
| P4 | **Sense** | `SYN_FLT_SENSE` | 0..1 → detector gain +6..+48 dB (§5.1) | linear-in-dB | 0.50 |
| P5 | **Attack** | `SYN_FLT_ATTACK` | 0..1 → 0.2..300 ms, `tA = 0.2·1500^t` ms | log | 4 ms (t≈0.41) |
| P6 | **Release** | `SYN_FLT_RELEASE` | 0..1 → 20..2000 ms, `tR = 20·100^t` ms | log | 180 ms (t≈0.48) |
| P7 | **Rate** | `SYN_FLT_RATE` | stepped 19-division synced list, 4 bar → 1/256 (§5.2) | stepped | 1/4 |
| P8 | **Sweep** | `SYN_FLT_SWEEP` | 0..1 → 0..±60 semis LFO depth | linear in semis | 0 |

**Dead-knob audit (law 5):** with defaults, Follow/Sense/Attack/Release are live the moment the
device powers on (instant auto-wah — the boot sound is the demo). Rate/Sweep live once Sweep > 0;
Track once > 0; Var live on every type (each relabel has an audible 0→100). Nothing plateaus: every
mapping above is strictly monotonic with no clamp-flat region (the `lvl/(1+lvl)` knee replaces
hard clipping in the env law for exactly this reason).

**No-doubles check:** no name above collides with another device's knob set (Delay owns Time/
Feedback; Distortion owns Drive… — ⚠️ Distortion's front knob is also **Drive**. Precedent check:
Reverb and Delay both ship **Mix**, and both Delay/Distortion ship **Tone**; the no-doubles law is
*within* a surface (a pane-box label vs its tab), not across devices. Keep Drive.)

---

## 8. §Visualizers

### 8.1 Survey — how the greats draw a filter

| Product | Mechanism (precisely) |
|---|---|
| **Serum 2** | Response curve on log-f axis; three right-click display modes — **Frequency Response**, **Frequency Response & FFT** (live input FFT rendered under/behind the curve so you watch the spectrum get shaved), **Phase Response & FFT**. The curve is INTERACTIVE: click-drag in the display moves cutoff (x) and res (y) simultaneously. The curve redraws live under modulation. (Serum 2 User Guide pp. 140–142.) |
| **FabFilter Volcano 3** | All filter curves on one graph; each filter's node shows a **real-time modulation trail** (the dot physically rides its XLFO/EG/EF excursions); interactive drag; per-filter color. |
| **Cytomic The Drop** | "Accurate frequency response" plot — the drawn curve is computed from the actual nonlinear model per block, and the **modulated position renders in real time** (env/LFO visibly move the curve, not a static overlay). |
| **Soundtoys FilterFreak** | Skeuomorphic — big cutoff dial + analog meter; motion is heard, not plotted. (The anti-pattern for us: fails law 9's "reflect every sound-changing param".) |
| **Arturia Filter MINI/SEM/M12** | 3-D hardware art + separate small scopes for each modulator (LFO shape, env follower level meter, sequencer lane). Motion sources visualized individually, response curve absent (MINI) or schematic (M12). |
| **Kilohearts Filter** | Minimal single response curve with drag handle — cheap, clear, static. |

Synthesis: the winning pattern is **Serum's live curve + FFT** fused with **Volcano's motion trail**.
We already own both halves: the synth filter analyzer draws all 94 per-type curves and glides a
live post-mod node (fb163 grammar), and the fb328 Distortion core proved the "engine-truth viz +
occupancy" pattern.

### 8.2 OUR card — proposals (canvas/SVG, CPU-cheap per fb342/fb343 law)

New native `getFilterFxViz` → `[cutHzL, cutHzR, res_eff, env01, lfoPh, keySemis, inLvl, typeIdx,
charIdx]`, pushed on the 60 Hz C++ timer lane (fb343 — no self-polling), drawn with write-gated
attrs, graduated halo trio, **zero shadowBlur/filters** (fb342 law), rAF gated by `__cardOnly`.

* **Concept A — The Breathing Curve (primary, ships).** The per-type response curve (reuse the
  synth analyzer's 94-curve math verbatim — Vowel draws 3 peaks, Comb draws the spike series,
  Phaser draws notches; the every-curve-must-move law is pre-paid). The cutoff node **rides the
  summed motion live** (env + LFO + key), gliding 0.35/frame in log-f space (fb163 constant).
  Idle = dim curve, `inLvl` gates brightness + halo intensity (law 9's obvious delta); Wide splits
  into twin L/R curves. Res raises the drawn peak; Drive warms the stroke color. Every
  sound-changing param is visible.
* **Concept B — Motion Trail (ships with A, ~20 lines).** Ring buffer of the node's last ~400 ms
  of (x = log f, y = peak height) positions, drawn as a fading polyline behind the node. Env
  motion draws jagged comets synced to the groove; LFO draws smooth arcs; Key draws steps on new
  notes. You can *read which driver is moving the filter* from the trail's shape — the card teaches
  the back panel.
* **Concept C — Shaved Spectrum (popped-card extra).** Serum's FFT fusion: the existing pre/post
  spectrum bins (`index.html:18372` analyzer, re-grep) rendered as a dim pre-filter silhouette with
  the post-filter energy filled bright purple under the curve — literally showing what the filter
  removes. Costs one extra polyline from data we already compute; gate it to the extended card only
  (CPU).

Idle/active delta (law 9, hard rule): idle = 12 % opacity curve, no node, no trail; first input
sample ramps to full brightness within 50 ms. A bloom you can barely see is a fail.

---

## 9. §Interplay — the device in the chain

* **Unity-through discipline.** At Drive 0 / Res 0 / Cutoff 100 % / Multi-Low: the +14/−14 pair
  nulls and the SVF passband is flat — **exact unity** (verify < −80 dB residual). The *default
  patch* (Ladder, cutoff 1.1 k, res 0.35, Follow +40) is deliberately NOT unity — a filter is
  subtractive and the boot sound should demo the wah (see §16 Q1 for Max's call). Power OFF
  default means nothing changes until the user asks.
* **Ordering wisdom** (surface in the manual, not enforced): Filter **before** Distortion =
  classic synth voice (filter shapes what the drive chews — res peaks become screaming formants).
  Filter **after** Distortion = wah pedal into amp (the auto-wah reads the distorted envelope —
  flattened dynamics = lazier wah; document that Sense compensates). Filter **before**
  Delay/Reverb = the tail keeps the filtered color; **after** them = the filter performs over the
  whole wash (DJ type's natural seat — one knob over the entire mix).
* **Spectrum/dynamics downstream:** LP types remove the HF that Delay's in-loop dampers would
  otherwise recirculate (stacking = darker faster); high Res inserts a narrow +12..20 dB peak that
  will *dominate* a downstream compressor's detector and can push the Distortion device into its
  drive knee 20 dB early — that interaction is a feature (acid), but §10's presets keep res ≤ 0.6
  when feeding DST presets.
* **Mono-sum:** Wide's ±3 st split partially combs on mono sum for Comb/Phaser types (their
  response is frequency-fine). Acceptable, documented; Wide defaults OFF.
* **⚠️ THE fb305/fb338 LANDMINE (exact edits) — THREE existing sites, SIX lines, plus a fourth site
  you author.** The draft listed only two sites and missed the **delay** block entirely. Verified
  today by `grep -n "EVERY send bus joins EVERY main-send exclusion" PluginProcessor.cpp`:

  | Site | Lines | Guarded by |
  |---|---|---|
  | Reverb main-send | `:7159` (L) / `:7161` (R) | `hallMainSend_` |
  | Distortion main-send | `:7326` (L) / `:7328` (R) | `! dstRouteActive_` branch |
  | **Delay main-send** ← *missed by the draft* | `:7358` (L) / `:7360` (R) | `dlyMainSend_` |
  | **Filter main-send** ← *new, you write it* | — | `fltMainSend_` |

  Add `+ (fltSendL ? fltSendL[i] : 0.0f)` / `…R…` to **all six** existing lines, and write the new
  site with **all four** buses in its sum. Missing only the delay site is silent on reverb- and
  distortion-only tests — exactly how this class of bug survives. Grep the sentinel comment at build
  time (line numbers drift).
* **FX order:** `SYN_FX_ORDER` is a 6-way permutation choice of 3 devices
  (`PluginProcessor.cpp:3488`, read as index per the choice law at `:5860`). Four devices = 24
  permutations — a UI and param-cardinality decision (session law ①: choice-param cardinality is
  append-trap territory). §16 Q3.
* **Latency parity:** the device's internal 64-sample compensation (§6.3) matches the shipped
  Distortion's decision exactly, so chain reordering never changes total reported latency (zero).

---

## 10. §Presets — 13 factory sketches

Format: name — intent — Type/Char, then the non-default values (knob 0–1 unless noted).

1. **Auto Wah** — the Mutron demo, boots great on keys/bass. Ladder/Classic 24. Cut .55, Res .55,
   Follow +.65, Sense .55, Atk 4 ms, Rel 220 ms, Mix 1.
2. **Touch Bass** — subtle funk opener. Acid/Squelch. Cut .45, Res .45, Follow +.45, Sense .6,
   Atk 2 ms, Rel 120 ms, Track .25.
3. **Duck Quack** — hard transient quack. Vowel/Morph, **Punch ON**. Cut .5, Res .6, Follow +.85,
   Sense .7, Atk 0.5 ms, Rel 90 ms.
4. **Acid Line** — 303 squelch that plays the keyboard. Acid/Squelch. Cut .35, Res .8,
   Var(Accent) .6, Track .5 (=100 %), Drive .35, Follow +.3.
5. **Talk To Me** — LFO vowel morph. Vowel/Morph. Cut .5, Res .55, Rate 1/2, Sweep .4, Follow 0.
6. **Club Sweep** — the DJ riser. DJ/Clean. Cut .5 (flat — perform the knob), Res .5,
   Var(Curve) .6, Rate 4 bar, Sweep 0.
7. **Pump Gate** — filter pump on 1/4s. Multi/Low 24. Cut .7, Res .3, Rate 1/4, Sweep .55
   (down-biased: Follow −.2 adds level-duck feel).
8. **Comb Keys** — the playable resonator (marketing reel). Comb/Karplus. Cut .5, Res .7,
   Track .5, Var(Damp) .4 — note-ons pluck via `excite()`.
9. **Silver Rain** — shimmering comb wash. Comb/Shimmer, **Wide ON**. Cut .6, Res .6, Rate 1/8,
   Sweep .18.
10. **Slow Tide** — classic phaser drift. Phaser/8, **Wide ON**. Cut .5, Res .5, Rate 2 bar,
    Sweep .6, Var(Spin) .5.
11. **Radio Poison** — lo-fi carrier trash. Ring/Radio. Cut .62, Res .3, Mix .7, Drive .3.
12. **Bell Machine** — SSB bells. Ring/**Shift**. **Cut .78** (⚠️ *was `.5` — corrected: on the Bode
    core `cut01 = .5` is EXACTLY zero shift, i.e. bypass; `.78` ≈ +250 Hz*), Mix 1, Follow +.2.
    **Track 0** — a linear-Hz shifter does not keytrack (§4.8).
13. **Scream Lead** — the MS-20 howl that dies with the note. Scream/MS-20. Cut .4, Res .93
    (gated self-osc), Drive .6, Var(Bite) .7, Follow +.35, Rel 400 ms.

Preset infra: the fb342 `.fxr-preset` grammar — admit `core:'flt'` in the preset gate (the one
boolean that killed DST presets for a build), pid `flt_<slug>`, factory table in the UI like
`DST_PRESETS`.

---

## 11. §CPU — budget + strategy

Measured anchors from this repo: the whole Distortion device (2× + ADAA + analog ODEs) lands
low-single-digit %; the ladder is 5 tanh/sample/channel (`TerrainFilters.h:149`).

Estimate at 48 k, one instance, arm64:

| Piece | Cost |
|---|---|
| FilterSlot heaviest type (Acid/Ladder) at 2×, stereo | ~20–30 tanh-class ops/sample ≈ 0.3–0.6 % of one core |
| Second parked SVF for the DJ type (§4.9) | ≈ 0.05 % — only in circuit on Type = DJ |
| Halfband pair (129-tap polyphase, fb342-vectorized) | ~0.3 % (measured class from DST) |
| Motion block (env, LFO, key glides) | < 0.05 % |
| Coefficient recompute under modulation | the real cost — see below |
| **Device worst-case total** | **≤ 1.5 %** of one core; typical ≤ 0.8 % |

* 🔑 **The recompute trap:** `setParams` change-gates its tan/pow/exp recompute (`:1447-1454`) —
  but under per-sample env/LFO motion the gate never passes and it recomputes EVERY sample at 2×.
  Mitigation (ship it from day one): quantize the summed `cutSemis` to **0.01 st** and `res_eff`
  to 1e-3 before the `setParams` call — restores the cache during near-idle motion, inaudible
  (0.01 st ≪ JND), zero cost when sweeping. (Weighted cache-key law, session law ⑦.)
* **Silence sleep:** the fb342 DST awake-head pattern — input below −90 dBFS for 0.25 s AND
  self-osc gate closed ⇒ skip the core entirely (flush first). Power OFF ⇒ full bypass, zero cost.
* **No Quality dropdown:** fixed 2× everywhere (§6.3) — the chassis has no third dropdown slot and
  the flat cost is already small. Acid at 4× is the one candidate tier if Max ever hears aliasing
  on scream sweeps (§16 Q4); it would be an internal auto-promotion, not a user knob.
* **Never oversample:** the viz path (native returns block-rate values), the motion block
  (block-rate + glides), the Mix/dry path.

---

## 12. Build order

1. **Engine skeleton** — `FilterFxEngine.h` (grammar of `DelayEngine.h`): owns one `FilterSlot`,
   `Halfband2x` pair, 64-sample dry delay, motion block stubs. Multi/Low 24 only. Gate: unity null
   at neutral (< −80 dB) + Mix wet-law check.
2. **Chassis** — `SYN_FLT_*` APVTS block, relays (4-point binding audit), back panel + front card
   on the v7 chassis, `CORES['flt']` with a static curve. Gate: every knob moves DSP (perceptual
   harness smoke run).
3. **Type roster** — all 9 types + Character rosters + Var relabels + fade-swap (§6.6). Gate: the
   §4 discriminator table measured green per type.
4. **Motion block** — env follower + calibration table (§5.1), synced LFO (one-clock), key track +
   `lastNoteFx_` plumbing + `excite()` hook. Gate: §13 motion rows.
5. **Send bus** — 4th bus + routing pills + **the fb305 exclusion edits (§9)** + order integration.
   Gate: main-send exclusion null test (the fb305 harness), pluginval + auval **exit code**.
6. **Viz** — `getFilterFxViz` native + Concept A/B card + idle/active delta. Gate: headless render,
   read the PNG, idle-vs-playing screenshot pair.
7. **Presets** — factory 13 + preset-gate boolean + user save/load. Gate: every preset audibly
   distinct on the standard chord loop.
8. **Certification sweep** — §13 full table, all 9 types × characters, then Max's ears.

---

## 13. Verify — the perceptual harness

Reuse the metric battery (the `rvb_perceptual.cpp` / `dst_cert_*` grammar; sample-diff RMS BANNED per
fb283 — magnitude-spectrum/centroid/HF-ratio/flux only). Compile-pattern:
`clang++ -O2 -I shim -I Source flt_cert.cpp` (the dst_cert family precedent).

⚠️ **`scratchpad/` is NOT in the repo** — verified: there is no `scratchpad/` directory anywhere
under the worktree, and no `rvb_perceptual.cpp` / `dst_cert_*` on disk. Those harnesses live in
**per-session scratchpads** (`/private/tmp/claude-501/…/scratchpad`, the Phase G one is `21b98786`)
and evaporate. Before quoting "reuse the harness": either recover them from that scratchpad or
budget re-authoring them. If they get recovered, **commit them into the repo this time** — every
bible in this folder now cites a path that does not exist.

| Gate | Metric | Pass |
|---|---|---|
| Unity null (Multi neutral, Drive 0) | residual RMS | < −80 dB |
| DJ noon null | residual | < −80 dB |
| Mix 100 % wet | dry residual | < −60 dB |
| Slope (Ladder 24) | fitted dB/oct over 2 octaves post-knee | −24 ± 1.5 |
| Acid discriminator | res-peak Q at fc = 200 Hz vs 2 kHz | ratio > 1.5× (ladder control < 1.15×) |
| Scream discriminator | THD of res peak, res .95 vs .6 | rises > 12 dB (Multi control: < 2 dB) |
| Vowel | **4** peak freqs vs `FormantBank::VF[v][0..3]` (`TerrainFilters.h:770-790`) | ± 3 % |
| Unity at Mix 0, main-send (§6.3 ⚠️) | residual RMS of `device_out − device_in`, Mix 0, Power ON | < −80 dB — **this is the row that catches the 64-sample insert comb** |
| Comb + Track 100 % | spike-1 freq vs played note | ± 1 cent |
| Phaser 8 | notch count / depth at internal mix | 4 notches ≥ 30 dB |
| Ring | sideband placement f ± fc; Bode: rejected sideband | exact bins; > 30 dB rejection |
| Env follower | step −26 → −16 dBFS program: cutoff excursion + timing | ≥ 2 oct; t₉₀ = Attack ± 20 % |
| Free-run gate | res 1.0, kill input | self-osc ≤ −60 dB within 1.0 s |
| Key glide | legato C2→C4, Track 100 % | no step > 0.5 st between consecutive 1-ms frames |
| Zipper (every knob) | 0→100 sweep in 50 ms while playing | spectral-flux ≤ honest per-type click floor (fb345 probe-craft: state per-type floors, PK_AM probe for Attack/Release/Punch — static-duck class) |
| Type switch | all 9×8 pairs mid-note | no transient > program peak + 3 dB (fb345 re-seat gate) |
| Level spread | all 13 presets, standard loop | within ± 6 dB band (the Phase G preset-spread lesson) |

**Dramaticism gate** (fb311): every knob's 0 vs 100 rendering must clear the night-and-day
thresholds (≥ 6 dB spectral change or ≥ 25 % centroid move — the DST §8.3 numbers) on the standard
−26 dBFS program, not on a hot lab tone.

---

## 14. §Pitfalls — collected traps

1. **The recompute-cache stall** (§11) — per-sample motion defeats the `:1447` gate; quantize the
   key or eat 3× the CPU.
2. **`setType` hard-reset click** (§6.6) — never call it hot; deferred fade only.
3. **Comb length snap** — comb cutoffs must glide in *delay-length* domain (the comb-click law
   class); the core already glides internally — do not bypass by re-preparing.
4. **DC out of asymmetric drive + ring carriers** — fs-aware 10 Hz blocker, not the 0.995 hardcode
   (`DistortionEngine.h:122-124` documents the trap).
5. **Self-osc DC latch** — the Phase G grid-leak class: gate states must env-track (squared
   release), or a rail-parked filter boots silent/latched.
6. **LFO phase accumulator drift** — one-clock law (§5.2); derive from ppq.
7. **Latency vs Mix (RE-STATED — the draft had it backwards).** Delaying the internal dry is *half*
   the job: the host-side insert `+= e·(wet − in)` must subtract the **same** delayed tap, or the
   device combs at intermediate Mix in main-send mode (`in[n−64] − in[n]`, nulls every ≈750 Hz at
   48 k). §6.3 MUST-RESOLVE. Inaudible at Mix 0/100 — the worst kind of bug — and it is *already*
   in the shipped Distortion path.
16. **`excite()` only fires on `Type::KARPLUS`** (`:2110`) — the Pluck/Bright characters silently
    don't pluck. Widen the guard.
17. **Bode ≠ carrier.** Cutoff on the Shift character is a bipolar ±2000 Hz *shift*, noon = zero.
    Any preset or key-track that treats it as an exponential carrier is wrong (§4.8).
18. **Choice cardinality at birth** — Type/Character must be sized for the FINAL roster (law C,
    §7). Sizing to exactly today's count while an open question proposes a 10th type is the trap.
19. **The sync list starts with `Free`** — 20 entries, not 19. Off-by-one against the delay list
    (§5.2).
20. **Cutoff clamp ceiling is `min(20 kHz, 0.45·fs_eff)`** — bare `0.45·fs_eff` at 2× lets
    modulation run to 43 kHz and flat-tops seven of the nine types (§5).
8. **fb305 exclusion** — §9; the double-count is quiet (+2 dB-ish on routed material) and will
   pass casual listening. Run the null harness.
9. **Choice-index law** — Type/Character read raw index; `lround(raw·N)` is the fb50 bug class.
10. **Relay 4-point binding** — every `SYN_FLT_*` param; a missing relay builds clean and no-ops.
11. **Denormals in idle combs/SVF tails** — flush on sleep + `ScopedNoDenormals`.
12. **Mono-sum of Wide** (§9) — document, default OFF.
13. **Env follower on distorted input** (§9) — flattened crest = lazy wah; this is physics, note it
    in the manual (Sense up compensates).
14. **Track holds after note-off** by design (law: nothing turns off by itself) — do NOT "fix" the
    held color as a bug later.
15. **UI line drift** — every `index.html:` ref here will be stale; re-grep symbols
    (`.fxr-core`, `CORES`, `drawInto`).

---

## 15. Hard-rule compliance checklist (laws 1–10, walked)

1. **Bus reality** ✅ — +14 dB lift derived from the measured −26 dBFS bus (§6.2); Sense table
   stated at −36/−26/−16 dBFS (§5.1); no literature range copied.
2. **Chassis** ✅ — 2 dropdowns (Type, Character) + 8 back knobs 4×2 + 3 front heroes + Mix (§7);
   pragmatic Title-case names, no jargon (Follow/Sense/Track/Sweep say what they do).
3. **Time params** ⚠️→✅ — Rate spans 4 bar → 1/256; **index alignment only holds if the 20-entry
   list including `Free` is cloned whole** (§5.2, corrected at audit).
4. **Mix + no-cut switches** ⚠️ — 100 % = fully wet (row §13); dropdowns fade (§6.6); **but the DJ
   type's noon crossing must NOT be a fade-dip** (§4.9 second-slot fix), and Mix < 100 % in
   main-send mode is **open** until §6.3 is resolved.
5. **Params evolve / night-and-day types** ⚠️→✅ — extremity table (§2), soft-knee env law, dead-knob
   audit (§7). Types: 9 with measured discriminators (§4, §13) **after** cutting the duplicate
   Bode "Shift Down" character at audit; watch the `cut01`-saturation plateau above 20 kHz (§5).
6. **Nothing free-runs + loop-gain ledger** ✅ — self-osc env-gated with the fb325 constants
   (§6.5); per-loop max-gain table (§6.4).
7. **No clicks** ✅ — glide table per knob (§7), fade-swap, comb glide, zipper gates (§13).
8. **CPU** ✅ — ≤ 1.5 % worst case, quantized cache key, silence sleep, fixed 2× rationale, the
   never-oversample list (§11).
9. **Audible ⇒ visible + dramatic** ✅ — Concept A reflects every param, idle/active delta
   specified, trail shows *which* driver moves the filter (§8.2).
10. **Recycle first** ⚠️→✅ — §17: the device is ~90 % existing certified code by design, but the
    audit found three "verbatim reuse" claims that are **not** free: `excite()` needs a type-guard
    edit, Comb `Damp` needs three edits, and the perceptual harnesses are **not in the repo**.

**RACK-WIDE LAWS (A–D) walked:**
* **A — zero lookahead / zero reported latency** ⚠️ **OPEN** — no lookahead anywhere and the device
  reports zero, but the 64-sample halfband group delay vs the sample-aligned exclusion sums is the
  §6.3 MUST-RESOLVE. Nothing ships until a §13 "unity at Mix 0, main-send" row is green.
* **B — no runtime param creation** ✅ — the `SYN_FLT_*` block is fully static; the Character roster
  is a fixed-size list with per-type *hiding*, never per-type re-creation (§7).
* **C — choice cardinality fixed at birth** ⚠️→✅ **after the audit fix** — Type 12 / Character 12
  with disabled tails (§7). `SYN_FX_ORDER` cannot grow in place: §16 Q3 is a **blocker**, not a
  nice-to-have — the 4th device cannot join the chain until Max picks 24-way-at-birth or a drag UI.
* **D — every send bus joins every exclusion sum** ⚠️ **THREE sites / six lines + a new fourth
  site** (§9, corrected at audit — the draft had two).

---

## 16. §Open questions for Max

1. **Boot voice:** default patch = instant auto-wah (Ladder, Follow +40 — my recommendation: the
   power-on IS the demo) or neutral-open unity? One-word answer changes §7 defaults only.
2. **LFO shapes:** sine-only (my rec — the 10-LFO mod-matrix system owns shapes) or add Tri/Saw/
   Square/S&H to the device? If yes, it costs the second Character-dropdown slot on some types or a
   pill.
3. **FX order at 4 devices** — **owned by `FX-CHAIN-BIBLE.md` §3.4; this device defers to it.**
   The 6-way `SYN_FX_ORDER` choice param can't grow in place. **[AUDIT] "append-only cardinality
   trap" was the wrong phrase** — it implies appending is allowed. It is not: cardinality is fixed
   at birth in *both* directions (fb342), because hosts normalize automation against `N` and our
   read path is `round(v·(N−1))`. The three legal shapes: (a) a **rank/drag-list ValueTree property**
   replacing the param — the chain bible's recommendation, click-free by construction, the only one
   that survives device #5's 120 permutations; (b) a **new** param born at choice(24); (c) pin the
   Filter to a fixed chain position. Not an option: re-declaring `SYN_FX_ORDER` at any other size.
4. **Acid 4×:** the core header asks for 4× (`:411`); I ship 2× (voice parity + the internal
   clamp). If scream sweeps alias to your ear, internal auto-4× on Acid only (+0.3 % CPU).
5. **A tenth type — "Sample Hold"** (`SAMPHOLD/±` exist): I cut it as Distortion-family ground
   (Downsample twin). Veto welcome if you want it here instead.
6. **Key-track source priority:** last-note (spec'd) vs lowest-note (bass-follows)? Last-note
   matches the synth's mono law; lowest-note reads better on chords for the Comb resonator.
7. **Pill 2:** Punch (transient wah — spec'd) vs **Duck** (env *ducks* the wet like the delay's) —
   both are one-liner swaps on the same follower.

---

## 17. Recycle inventory (read-verified, reuse verbatim)

| Existing asset | Where | Reused for |
|---|---|---|
| `FilterSlot` + all 94 type cores | `TerrainFilters.h:1386-2195` | THE device core — hosted, not rewritten |
| `cutKnobToHz` / `driveLinear` / `driveMakeup` | `TerrainFilters.h:52-65` | front Cutoff map, Drive law (§6.2) |
| `LadderPoleMix` | `TerrainFilters.h:251` | Ladder Var = Slope |
| `SvfMultimode::setDrive` / `setMorph` | `:363` / `:2103` | Scream Bite, Multi Morph |
| `FormantBank::setVowel/setMorph` | `:824/:837` | Vowel Shift + Morph char |
| `FilterSlot::excite` | `:2108` | Karplus note-on pluck — ⚠️ **needs the `:2110` type guard widened**, not verbatim reuse |
| `DampComb<>::process` + `case COMB_DAMP` | `:1236-1242` + `:1764-1772` (`damp` hard-coded `0.5f`) | Comb **Damp** Var — ⚠️ *not* `:1265`, which is `CombReverb` (§4.6) |
| semitone mod-sum + 2.5 ms lane glide + keytrack law | `SynthVoice.h:4113-4130, :4088, :4195` | §5 motion summing |
| `coefSr` 2× doubling scheme | `SynthVoice.h:4080-4084` | §6.3 |
| Halfband 2× (129-tap, vectorized) + internal-latency pattern | `DistortionEngine.h:74-97` (+ §4.4 fix) | §6.3 `Halfband2x` |
| Input-env free-run gate — coefficients **2 ms / 60 ms**, applied **squared** ⇒ ~0.4 s audible close | `DistortionEngine.h:161-163` | §6.5 self-osc gate |
| Deferred char fade (`dnDip_`) | `DistortionEngine.h:156` | §6.6 type fades |
| fs-aware 10 Hz DC blocker | `DistortionEngine.h:122-125` | §6.7 |
| Env-follower one-pole idiom | `DelayEngine.h:213-218, :133-134` | §5.1 detector (+ its 4 ms/180 ms as defaults) |
| Synced-division table 4 bar→1/256 | `PluginProcessor.cpp:3456-3459, :7237` | §5.2 Rate |
| Send-bus + exclusion grammar (**THREE** sites: `:7159/:7161`, `:7326/:7328`, `:7358/:7360`) | `PluginProcessor.cpp:7144-7166, :7316-:7340, :7350-:7362` | §9 4th bus |
| Perceptual harnesses (`rvb_perceptual.cpp`, `dst_cert_*`) | ⚠️ **NOT in the repo** — session scratchpads only (§13) | §13 — budget recovery or re-authoring |
| Filter analyzer curve math + live-node glide (fb163) | `index.html:25049-25090` (re-grep) | §8.2 Concept A |
| Pre/post spectrum bins | `index.html:18372` (re-grep) | §8.2 Concept C |
| FX rack chassis, `CORES`, preset menu, pills, halo trio | `index.html:7229-7500` region (re-grep) + `Design/fx-rack-v7-CANONICAL.html` | §7/§8/§10 UI |
| Serum 2 Filter-FX param/viz breakdown (in-tree, read it before re-researching) | `Design/SERUM2-FX-REFERENCE.md §2.8` | §1.4 / §8.1 |

---

## 18. §Sources

**Primary manuals / official**
* Serum 2 User Guide (Xfer Records) — filter types + Var table pp. 136–140, filter params/display
  pp. 140–143, FX Filter p. 170. Local extract read in full; distributed via https://xferrecords.com
* Serum (1) Manual — FX Filter precedent: https://s3.amazonaws.com/decembercymatics/Serum_Manual.pdf
* FabFilter Volcano 3 Help — filter controls: https://www.fabfilter.com/help/volcano/using/filtercontrols ·
  envelope follower (Transient mode): https://www.fabfilter.com/help/volcano/using/ef ·
  modulation: https://www.fabfilter.com/help/volcano/using/modulation ·
  product: https://www.fabfilter.com/products/volcano-3-filter-plug-in
* Cytomic The Drop (circuit models, Safe, oversampling): https://cytomic.com/product/drop/
* Cytomic technical papers (Simper — SVF/SKF trapezoidal): https://cytomic.com/technical-papers/ ·
  https://www.cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
* Arturia Filter MINI overview: https://www.arturia.com/products/software-effects/mini-filter/overview
* Arturia Filter M12 overview + manual: https://www.arturia.com/products/software-effects/m12-filter/overview ·
  https://dl.arturia.net/products/m12-filter/manual/filter-m12_Manual_1_2_0_EN.pdf
* Arturia Filter SEM overview + manual: https://www.arturia.com/products/software-effects/sem-filter/overview ·
  https://dl.arturia.net/products/sem-filter/manual/filter-sem_Manual_1_2_1_EN.pdf
* Soundtoys FilterFreak product + manual: https://www.soundtoys.com/product/filterfreak/ ·
  https://static.bhphoto.com/lit_files/89646.pdf
* Kilohearts Filter / Ladder Filter / Nonlinear Filter: https://kilohearts.com/products/filter ·
  https://kilohearts.com/products/ladder_filter · https://kilohearts.com/products/nonlinear_filter

**DSP papers / analysis**
* Huovilainen, "Non-Linear Digital Implementation of the Moog Ladder Filter," DAFx-04:
  https://dafx.de/paper-archive/2004/P_061.PDF
* Bencina, Simper-SVF BIBO analysis (time-varying stability):
  http://www.rossbencina.com/static/junk/SimperSVF_BIBO_Analysis.html
* Simper, ADC 2020 "Circuit to Code" (Sallen-Key/MS-20 modeling) — slides via cytomic.com/technical-papers/

**History / circuits**
* GEO/geofex, "The Technology of Auto-Wahs / Envelope-Controlled Filters":
  http://www.geofex.com/Article_Folders/ECFtech/ecftech.htm
* GEO/geofex, "The Technology of Wah Pedals": http://www.geofex.com/article_folders/wahpedl/wahped.htm
* ElectroSmash, Vox V847 wah analysis (450–1600 Hz, +18 dB peak): https://www.electrosmash.com/vox-v847-analysis
* Perfect Circuit, "A Brief History of Mu-Tron": https://www.perfectcircuit.com/signal/mu-tron-history
* Wikipedia, Mu-Tron III: https://en.wikipedia.org/wiki/Mu-Tron_III
* Guitar World, wah pedal history (1966 Warwick/Kushner/Plunkett):
  https://www.guitarworld.com/features/wah-pedal-history-evolution
* Wikipedia, Roland VP-330 (formant band centers): https://en.wikipedia.org/wiki/Roland_VP-330

**Open source / comparative**
* Vital (mtytel) — ladder/SVF/comb/formant/phaser filter sources: https://github.com/mtytel/vital
* Xfer DJMFilter (one-knob bipolar law): https://www.audiotechnology.com/free-stuff/xfer-records-djm-filter ·
  https://equipboard.com/items/xfer-djmfilter
* Serum 2 filters overview (secondary): https://monosounds.studio/serum-2-filters-explained/

---

*End of bible. A builder should be able to implement `FilterFxEngine.h`, the `SYN_FLT_*` block, the
rack card, and the harness from this file plus the cited lines — without re-research.*
