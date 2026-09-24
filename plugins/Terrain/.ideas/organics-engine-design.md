# Organics Engine: Design

**Project:** Terrain (Waves Crate). This is oscillator engine #8, `Engine::ORGANIC = 7`.
**Base:** `feature/terrain-instrument` @ `3e5e124`
**Date:** 2026-09-24
**Owner:** Max
**What it is:** a curated **factory library** of multi-sampled acoustic instruments: pianos, EPs, strings, winds, brass, mallets, bells, guitars, basses, organs, clav, choir. It plays inside any of Terrain's 8 oscillators (A–H). It is **not** a user sampler (Serum's Multisample or DirectWave) and **not** physical modelling (that is Modal). Every knob goes through the mod matrix, the per-voice filters, the FX rack and the Patcher, like every other engine.
**Read with:** `soundfont-research.md` (licences and the cleared library shortlist), `sample-engine-v1-design.md`, `serum2-sampler-research.md`.

---

## 0. Summary of decisions

| # | Decision | One-line reason |
|---|---|---|
| 1 | **Page 1 knobs: `Dynamics · Tone · Body · Attack · Human`.** Page 2: `Release · Noise · Sustain · Velocity · Image`. The unison page is unchanged, but on this engine it becomes an **Ensemble** (each unison voice is a different *player*, not only a detuned copy). | Each page-1 knob does something only a sampler can do, and each one works on every instrument. |
| 2 | **Ship ONE compiled Terrain format, `.torg`.** Each instrument is a folder with `map.json` (a normalised region list) and `samples/*.flac` (16-bit, lossless, unencrypted). The original SFZ/SF2 plus licence is kept beside it in `source/` for provenance. | The runtime needs **no SFZ or SF2 parser**. It only reads JSON and decodes FLAC with JUCE's built-in reader. CC-BY content stays plain FLAC, which the licence requires. |
| 3 | **An offline compiler, `Tools/organics/torgc.py`, does the SFZ/SF2 work.** It covers the SFZ opcode subset that the target libraries actually use (measured: 23,297 regions across 8 libraries). For SF2 it uses TinySoundFont's hydra logic (MIT) as the reference. **Neither sfizz nor TSF goes into the plugin.** | sfizz is 3.2 MB of C++ plus Abseil, and was archived on 2026-06-21. TSF uses linear interpolation, has no loop crossfade and no release triggers. Both would duplicate Terrain's filters, envelopes and mod matrix. |
| 4 | **Playback is our own `OrganicsEngine`.** It reuses the proven Hermite reader, the adaptive equal-power loop crossfade and the declick from `SampleEngine.h`. It adds region picking, velocity crossfades, round robins, release triggers, choke groups and articulations. | Owning ~900 lines is cheaper than owning 100k. It also slots into `renderXxxOsc` exactly like Modal and Harmonic. |
| 5 | **CPU target: the engine core costs ≤ 0.35× a wavetable unison voice.** An 8-note chord at unison 1 must cost ≤ the same patch on WT in `Tests/au_cpu_profile.cpp`. The dice weight is `ENGW[7] = 0.5` (the same as WT). | A sample reader is about 8 multiply-adds per output frame. Most of the cost of a note is the per-voice filter, envelopes and mod application (tp32). |
| 6 | **Memory, day 1: the instrument is RAM-resident as int16, with an authoring budget** (a typical instrument ≤ 48 MB, a flagship piano ≤ 160 MB). It is shared by refcount across all 8 oscillators and both banks, and freed 5 s after its last user (the tp63 lazy-release precedent). **v1.1 adds direct-from-disk streaming** (8,192-frame preload, the sfizz default) for the flagship pianos only. | Streaming is the one piece that cannot be proven safe in a day. The reader API is designed now so that streaming slots in later without touching the voice. |
| 7 | **Visualiser: a "line-kit".** Each instrument family is a few dozen lines of **parametric 3D primitives** (box, extruded outline, lathe, tube, string, key row). They are projected once to a fixed ¾ isometric SVG, drawn white at 1 px, with hidden lines removed by painter's order. Reactive parts (keys, strings, bars, valves, bells) are separate primitives redrawn **only on note events**, so an instrument at rest costs 0 frames. **22 families.** | This gives a true 3D, consistent Teenage-Engineering-style drawing without mesh assets or a z-buffer. It is cheap enough to obey Terrain's "at rest = 0 frames" law, and agents can author it (a family is code, not an illustration). |
| 8 | **The engine choice grows from 7 to 12 in one go.** Index 7 is Organic and 8–11 are reserved ("—", hidden). | Growing the choice shifts host automation lanes one time (RACK LAW C, `P:109-118`). Reserving the slots now means it never happens again. Saved presets are safe because they store the index (`PresetCarries.h:113-121`). |
| 9 | **Build: 4 agents in 4 git worktrees, one day.** The file ownership is disjoint (§7). Interfaces are frozen in the first 30 minutes by the lead, using this doc's §5–§6 contracts. | The merge order is DSP, then integration, then UI, then content. There are no shared files except the three the integration agent owns. |

---

## 1. What makes a sampled instrument sound great, not cheap

The research below drives the day-1 cut. The playback features that separate "Kontakt / Keyscape / LABS" from "General MIDI ROMpler" are, roughly in order of audibility:

1. **Enough velocity layers, and a crossfade between them.** A hard switch at layer boundaries is the #1 cheap tell: timbre jumps at velocity 64 and 65. The answer is equal-power `xfin/xfout_lovel/hivel`-style crossfades, and a **continuous Dynamics control** that can sweep the layer while a note is held. LABS puts this on the mod wheel ("smoothly fading between the recorded velocity layers", [MusicRadar on LABS](https://www.musicradar.com/news/fantastic-free-plugins-spitfire-audio-labs)).
2. **Round robins.** Without them, repeated notes sound like a machine gun. Real RR uses `seq_length/seq_position` or random `lorand/hirand`. When a set has no RR, **fake RR** gives believable variation: ±3–5 cents, a neighbour-zone "borrow" (play the sample one semitone away, repitched), and small start-offset and filter variation ([VI-Control: faking RR](https://vi-control.net/community/threads/how-to-create-round-robin-with-a-single-sample.141296/), [KVR RR simulator](https://www.kvraudio.com/forum/viewtopic.php?t=555002)). A no-immediate-repeat rule applies to random RR.
3. **Release samples (`trigger=release`) with `rt_decay`.** The damper and string stop on a piano, the finger lift on a guitar, the room tail. Without them every note ends in a synthetic fade. `rt_decay` lowers the release sample's level by how long the note was held ([sfzformat: rt_decay](https://sfzformat.com/opcodes/rt_decay/)).
4. **A clean onset, including "air".** A sample often starts with 5–40 ms of pre-attack noise. Keyscape exposes this directly as Attack *Natural / Gentle / Tight*: "the amount of 'air' before the sample is triggered" ([Keyscape Feel](https://support.spectrasonics.net/manual/Keyscape/11/en/topic/feel)).
5. **Interpolation quality and aliasing when pitched up.** Linear interpolation (TinySoundFont's only mode, `tsf.h` lines 1294–1339) images and aliases. A 4-point Hermite is the practical real-time floor. Windowed sinc (sfizz offers Sinc8…Sinc60) is the HQ path ([KVR: sinc vs hermite](https://www.kvraudio.com/forum/viewtopic.php?t=501053), [sfzformat sample_quality](https://sfzformat.com/opcodes/sample_quality/)). The biggest win is **mapping so that zones are rarely pitched up by more than 2–3 semitones**. That is a compile-time guarantee, and it costs no CPU.
6. **Loop crossfades on sustaining instruments** (strings, winds, organ, choir). Seams click or "breathe" without them. sfizz implements `loop_crossfade` with a sine curve, and most players do not implement it at all ([sfzformat loop_crossfade](https://sfzformat.com/opcodes/loop_crossfade/)). Terrain already has an auto-scaled, equal-power crossfade in `SampleEngine.h:223-305`.
7. **Velocity to brightness, independent of velocity to volume.** Real instruments get brighter as well as louder. When the layers are few, a velocity-tracked tilt fills the gap between them. **Key-tracked filtering** tames the brittle top octave of stretched samples.
8. **Mechanical noise.** Hammer and key noise, pedal thump, fret squeak, breath, valve clack, and release noise, each on its own level. Noire exposes Pedal / Mechanical / Felt noise separately. Keyscape has Pedal Noise and Release Noise with a transition time ([Noire review, SOS](https://www.soundonsound.com/reviews/native-instruments-noire), [Keyscape Noise](https://support.spectrasonics.net/manual/Keyscape/11/en/topic/noise)).
9. **Sustain pedal behaviour.** CC64 holds notes. **Sympathetic or pedal resonance** (the open-string halo) and **half-pedal** make a piano feel physical. Osiris ships separate pedal-down sample sets (`sustain_cc`, `locc64/hicc64`, measured below).
10. **Stereo image control.** Sampled pianos are recorded very wide. Stacked in a synth with 8 oscillators, they need a width control (mono, as recorded, or wider).
11. **Legato and portamento for monophonic winds and brass.** `trigger=legato` regions or a transition crossfade. This is a later feature: Terrain's mono and glide modes cover the basic case.
12. **Humanisation.** Tiny timing, pitch and level scatter. It must be *per note*, never per block.
13. **True release vs envelope release.** Sustaining instruments should keep playing their loop through the envelope release. Decaying instruments (piano, guitar, bells) should keep their **natural decay** while the key is held and apply the release on note-off. Hard-truncating the sample (a missing "loop_sustain vs one_shot" distinction) is a classic cheap tell.

**What the target libraries actually use.** I downloaded every `.sfz`/`.sfzh` from Salamander, Headroom, Greg Sullivan EPs, Osiris, Karoryfer Meatbass, Weresax and Emilyguitar (23,297 regions, 159 distinct opcode forms) and counted them. Excerpt:

`sample 23297 · hikey 11175 · pitch_keycenter 11010 · lovel 9641 · lokey 9603 · hivel 8995 · amp_velcurve_N 8336 · hirand 8088 · lorand 6437 · seq_position 4223 · pan_onccN 3737 · tune_onccN 3714 · ampeg_release 3465 · group 3069 · off_by 2980 · locc 2204 · trigger 2111 · volume 1803 · hicc 1642 · sw_last/sw_lokey/sw_hikey 1540 · delay 1223 · seq_length 932 · loop_mode 724 · pan 686 · off_mode 514 · offset_random 380 · rt_decay 19 · amp_random 18`

Plus a long tail of ARIA `lfoNN_*`, `egNN_*`, `eqN_*` and `*_onccN` performance modulation (Karoryfer). **That tail is exactly what Terrain's own LFOs, envelopes, filters and mod matrix replace.** So the compiler bakes the mapping, and the performance layer becomes Terrain's knobs.

---

## 2. The knob set

### 2.1 What the others expose (grounding)

| Product | Front-panel controls |
|---|---|
| **Serum 2, Multisample osc** | Loads SFZ (key zones and velocity layers). It reuses the generic oscillator strip: tuning OCT/SEM/FIN/CRS, unison voices/detune/blend with the new unison tuning modes, two warps, pan, level. **No instrument-specific performance controls, and no editor.** Dubspot calls it "the most 'version 1.0' feature in the box" ([Dubspot review](https://blog.dubspot.com/xfer-records-releases-serum-2), [Xfer product page](https://xferrecords.com/products/serum-2), [Sonic Weaponry](https://sonic-weaponry.com/blogs/free-production-tutorials-and-resources/serum-2-released)). *The exact control list was not extractable from the official manual text; confirm in-app.* |
| **Spitfire LABS** | **Dynamics** (a crossfade across the recorded velocity layers, on CC1), **Expression** (CC11 volume), and one big multi-knob (Reverb / Variation / ADSR depending on the instrument) ([MusicRadar](https://www.musicradar.com/news/fantastic-free-plugins-spitfire-audio-labs), [9to5Mac](https://9to5mac.com/2020/04/26/free-instrument-library-labs/)) |
| **Arturia Augmented** | **Morph** (the big knob, layer A to B) plus Time, Color, Motion, FX A, FX B, Reverb, Delay ([Arturia](https://www.arturia.com/products/software-instruments/augmented/strings), [KVR](https://www.kvraudio.com/product/augmented-strings-by-arturia)) |
| **NI Noire** | Dynamics (compress/expand), **Color (shifts the sample mapping)**, **Tonal Shift** (speed plus formant), Reverb, Delay. The edit page has release samples, body resonance, overtones (sympathetic), pedal / mechanical / felt noise, and Attack ([SOS](https://www.soundonsound.com/reviews/native-instruments-noire)) |
| **Keyscape** | Attack (Natural/Gentle/Tight "air"), Rel Transition, Rel Time, Pedal Noise, Release Noise, Tone, Damper… ([Keyscape manual](https://support.spectrasonics.net/manual/Keyscape/11/en/topic/feel)) |
| **Decent Sampler / Pianobook** | Tone (often on the mod wheel), Attack, Release, Reverb, Chorus, LPF ([decentsamples](https://www.decentsamples.com/product/reverb-tank-violin/), [Pianobook](https://www.pianobook.co.uk/sampler/decent-sampler/)) |

Three patterns recur: **dynamics across layers**, **tone**, and **attack/release character**. Reverb, delay and chorus recur too, but Terrain already has a full FX rack, so they would be dead weight here. Noire's **Color** stands out for Terrain: it changes timbre by *borrowing a neighbour's sample* and costs nothing.

### 2.2 Candidate sets

| Set | Knobs | Verdict |
|---|---|---|
| **A: "Performer"** | Dynamics · Tone · Attack · Release · Humanize | Solid, LABS-like. But **Release** overlaps Terrain's amp envelope, and there is **no timbre-without-pitch** control, which is the one thing that makes a sampled piano sound "designed" inside a synth. |
| **B: "Macro"** (Arturia-like) | Morph · Color · Motion · Time · Space | Each macro drives several targets, so it is opaque. Motion duplicates Terrain's LFOs and Space duplicates the FX rack. It breaks Terrain's "one knob = one clear DSP thing" house style. **Reject.** |
| **C: "Organic"** ✅ | **Dynamics · Tone · Body · Attack · Human** | Every knob is (a) something only a sampler can do, (b) meaningful on every instrument family, (c) a clean mod destination, and (d) nearly free in CPU. Release/Noise/Sustain move to page 2, where envelope-adjacent controls belong. |

**Pick C.** Page 1 is the *performance* of the sound: which layer, how bright, how big, how it starts, how human. Page 2 is the *mechanics*: how it ends, its noises, whether it sustains, how it responds, how wide it sits.

### 2.3 Page 1: `Dynamics · Tone · Body · Attack · Human`

All values are normalised 0..1 in APVTS (step 0.001). "Bipolar" means centre 0.5 = off, and the ring fills from 12 o'clock (the same rendering as Sample's Scan/Formant).

| Knob | Param | Range (display) | Default | What it does to the DSP |
|---|---|---|---|---|
| **Dynamics** | `SYN_OSC_x_ORG_DYNAMICS` | bipolar −1…+1 ("−64…+64 vel") | 0 | Offsets the **layer-selection velocity**: `vL = clamp(vel + 63·d, 1, 127)`. Loudness still follows the *played* velocity through the Velocity knob's curve. Moving it while a note is held **crossfades live between the adjacent layers**. Each note keeps the two regions bracketing `vL` running, with equal-power gains, so sweeping it or driving it from the mod wheel or an LFO is LABS-style dynamics. Layer loudness is RMS-normalised at compile time (`region.gainNorm`), and 50 % of the natural loudness difference is restored, so a layer shift reads as a *timbre* change with a gentle level change rather than a jump. Instruments with CC-driven dynamics (`xfin_locc1`…, string and wind sustains) map their CC axis onto this knob. Cost: at most 2 readers per note while between layers, 1 at a boundary. |
| **Tone** | `SYN_OSC_x_ORG_TONE` | bipolar, −1 dark … +1 bright ("±9 dB tilt") | 0 | A velocity-aware **first-order tilt**, pivot 700 Hz: gain at 20 kHz = `(9·t + 4·(vel−0.6)·velTrack) dB`, clamped to ±12 dB, and 0 when vel = 0.6 with t = 0. It adds **key tracking** above C6 (−1.5 dB per octave on the top, with a slight darkening that removes the "tizz" of stretched top zones). It is one 1-pole shelf per voice (stereo), with coefficients recomputed per block and only when the change is more than 0.05 dB (the fb441 perceptual-delta law). |
| **Body** | `SYN_OSC_x_ORG_BODY` | bipolar −1…+1 ("−6…+6 st") | 0 | **Noire-style Color.** The zone lookup uses `note + round(6·b)` and **repitches the chosen sample back to the played note**. + borrows *higher* samples pitched *down*, so formants drop and the instrument sounds bigger and darker. − borrows lower samples pitched up, so it sounds smaller and nasal. The effect is formant-shift-like at **zero DSP cost** (a different lookup and a different ratio). Fractional values crossfade the two neighbouring shifts, so modulation is smooth. Upward repitch is clamped to +7 st total per zone (aliasing guard, §4.4). |
| **Attack** | `SYN_OSC_x_ORG_ATTACK` | bipolar: −1 Gentle … 0 Natural … +1 Tight | 0 | Keyscape's "air" axis, built on **compile-time onset markers** (`region.onset`, the frame where the transient crosses −24 dB of the region peak). **Tight (+):** the start offset moves toward `onset − 1.5 ms` (the air is removed), plus a transient lift of up to +4 dB over the first 12 ms (a gain ramp, no dynamics processor). **Natural (0):** the sample as recorded. **Gentle (−):** a raised-cosine fade-in of 0–150 ms, and the first 60 ms crossfade toward the *next-softer* layer (felt, bowed, breathy). This is sample-specific and different from the amp envelope's attack, which still exists and stacks. |
| **Human** | `SYN_OSC_x_ORG_HUMAN` | 0…1 | 0.25 | One knob scales **per-note** variation, fixed at note-on (the NoteOn-random seed already used by Spray): ±`4·h` cents detune; ±`1.5·h` dB level; a start offset of 0…`6·h` ms beyond the onset; ±`1.5·h` dB tone; a **timing delay** of 0…`12·h` ms (a render-silence countdown inside the voice, no scheduler). **Round robin always runs** (real RR when the set has it). When the set has **no RR**, Human > 0 enables **fake RR**: 1 in 3 notes borrows the ±1 st neighbour sample (repitched), and the choice is no-repeat. At 0 the result is fully deterministic, which the bit-identity and determinism gates need. |

### 2.4 Page 2: `Release · Noise · Sustain · Velocity · Image` (thin arrow → `.organic-pg2`)

| Knob | Param | Range | Default | DSP |
|---|---|---|---|---|
| **Release** | `SYN_OSC_x_ORG_RELEASE` | 0…1 (0 = off, 0.5 = authored, 1 = +6 dB) | 0.5 | Sets the level of **`trigger=release` regions** (damper, finger lift, room tail), with `rt_decay` honoured. For **decaying** instruments it also sets the **natural-decay handoff**: at note-off the voice keeps its sample and fades over `max(ampEnvRelease, 40 ms + 400 ms·r)`. It never hard-cuts. Loop-sustain instruments keep looping through the envelope release (true release). |
| **Noise** | `SYN_OSC_x_ORG_NOISE` | 0…1 (0.5 = authored) | 0.5 | The level of **mechanical-noise regions**, which the compiler tags `kind=noise`: hammer/key-off, pedal up/down, fret squeak, breath, valve clack. When an instrument has no noise regions the knob is greyed with the tooltip "this instrument has no mechanical noise". It stays usable and does nothing, **never a fake noise**. |
| **Sustain** | `SYN_OSC_x_ORG_SUSTAIN` | 0…1 | 0 | **Synth-y, and universal.** 0 = the natural decay. As the value rises, decaying instruments crossfade into a **compile-time tail loop** (a stable 150–600 ms window picked by autocorrelation near the −30 dB point, equal-power crossfade) and the decay rate is reduced by `(1−s)`. At 1, a piano or bell **holds forever**, like an e-bow. Sustaining instruments treat it as the loop-mode override "hold loop through release". |
| **Velocity** | `SYN_OSC_x_ORG_VELOCITY` | 0…1 | 0.75 | Velocity-to-amplitude depth (`amp_veltrack`): 0 = every note at full level, 1 = the instrument's authored `amp_velcurve`. Right-click offers **Soft / Linear / Hard** curve presets. It also sets the velocity-to-brightness depth used by Tone. |
| **Image** | `SYN_OSC_x_ORG_IMAGE` | 0…1.5 ("mono…as recorded…wide") | 1.0 | Mid/side width of the sample itself, **before** unison spread. 0 = mono (best when you stack 3 oscillators). 1 = as recorded. 1.5 = +50 % side. The multiply happens per voice. At 1.0 it is skipped. |

**Header, not knobs:**
- **Instrument pill** (opens the browser, §6.4): `SYN_OSC_x_ORG_INST`.
- **Articulation** select, shown only when the instrument has more than one (Sustain / Staccato / Pizz / Tremolo / Mute…). It comes from SFZ keyswitches `sw_*`, which the compiler turns into named articulations: `SYN_OSC_x_ORG_ARTIC`.

### 2.5 The unison page ("Ensemble" on this engine)

The unison controls are shared by all engines (`Voices · Stack · Range · Detune · Blend · Width`, `H:9117`) and keep their IDs. The Organic engine **interprets** them the way Modal interprets them inside `renderModalOsc` (`V:8368`):
- **Voices** = *players*. Each unison sibling is a separate note instance with its **own RR pick** (sibling k takes the RR index `rr + k`), its own Human seed, and a start/timing offset of `k · 7 ms · Detune` (a *section* instead of a chorus). Eight players on a solo violin gives a believable small section. Eight on a piano gives a "double-tracked" pair of pianos.
- **Detune** keeps its meaning (the pitch spread), but the **default scale for this engine is 0.25×**, because 25 cents on a piano is out of tune and not "fat".
- **Stack** (12 / 12+7 / centre −12) = octave doubling, like a string section adding the celli. It works unchanged.
- **Blend / Range / Width** are unchanged.

Cost: each player is one more sample reader (about 8 MACs per frame per region). This is the cheapest unison in Terrain.

**Parameter count:** 12 per oscillator (10 knobs plus INST plus ARTIC) × 4 declared (A–D) = **48 declared**. E–H are cloned automatically by the pool LayoutTap (`P:7674-7715`), so there are **96 new APVTS parameters**. The total goes from 5,655 to ~5,751.

---

## 3. Playback features: day 1 vs later

**Day 1 (must ship):**
- Key/velocity zone lookup, from a precomputed `[articulation][128 keys][128 velocities] → regionSpan` table built at load. The note-on lookup is O(1).
- **Velocity crossfade**, including the live Dynamics crossfade (equal-power).
- **Round robin**: sequential (`seq_*`), random (`lo/hirand`) with no immediate repeat, and **fake RR** through Human.
- **Release triggers** with `rt_decay`, and the Release knob.
- **Loop modes:** `no_loop`, `one_shot`, `loop_continuous`, `loop_sustain`. The compiler-authored crossfade uses the `SampleEngine` equal-power math.
- **Choke groups** (`group` / `off_by` / `off_mode fast|normal`): hi-hats, mono winds, and same-note retrigger on piano. A choke is always a **5 ms fade**, never a cut (the house law).
- **Articulations** (keyswitch sets compiled to named articulations, switched by the Artic param, not by keys).
- **Sustain pedal CC64 hold**, handled the way Terrain's voices already handle it. Libraries with pedal-down sample sets use them (compiled as articulation variants selected by the pedal state).
- **Mechanical-noise regions** with the Noise knob.
- Onset markers (Attack), tail loops (Sustain), and RMS gain normalisation.
- **4-point Hermite** in real time. **8-tap windowed sinc when `isNonRealtime()`** (offline bounce), the same live/freewheel split sfizz uses.
- Voice stealing inside the engine: the oldest releasing region goes first, with a 5 ms fade. Per-oscillator cap: **48 regions** (players × layers × release).

**Later (v1.1+), in priority order:**
1. **Direct-from-disk streaming** for the flagship pianos (§4.2).
2. **Sympathetic / pedal resonance.** One shared resonator per *oscillator* (not per voice): 12 tuned damped combs fed by the oscillator's sum and gated by CC64. Cost is per block and does not scale with polyphony. Would ship as a page-2 right-click "Halo" amount.
3. **Half-pedal** (CC64 continuous → release-time scaling).
4. **Legato transitions** for mono winds and brass (`trigger=legato` regions or a 60 ms transition crossfade).
5. **2× decimated "mip" copies**, built lazily for zones played more than 5 st above their root (§4.4).
6. Importing a user's own SFZ/SF2. The compiler is embeddable, but this is **not** the product (curated library first).

---

## 4. Format and player choice

### 4.1 Format: SFZ vs SF2/SF3 vs our own

| Option | For | Against |
|---|---|---|
| Ship SFZ + FLAC as-is and parse at runtime | Human-readable; the libraries are already SFZ | Needs a runtime parser for `#define`, `#include`, `<master>`, ARIA extensions and curves; slow cold parse (Karoryfer uses 163 `#include`s); pulls ARIA modulation we would ignore anyway |
| Ship SF2/SF3 | One file per bank | 16-bit only (24-bit needs `sm24`); Vorbis in SF3 is **lossy**; the modulator model duplicates ours; the CC-BY "no DRM" rule is fine but the tooling is awkward |
| **Our own compiled `.torg`** ✅ | Runtime is a JSON read plus FLAC decode (JUCE built-in, `SampleLoader.h:74` already registers the basic formats); every value is resolved and normalised in advance (inheritance flattened, tunings in cents, times in seconds); stores **Terrain-only metadata** (onset, tail loop, RMS norm, noise tags, visualiser family); we choose layers and trims per memory budget | We maintain a compiler (offline Python, not shipped) |

**`.torg` v1 layout (frozen contract):**
```
Organics/
  index.json                       # [{id, name, family, category, tags, sizeMB, licence, credit}] - the browser reads ONLY this
  ids.json                         # append-only {"vcsl.mallets.vibraphone": 17, ...} → ORG_INST int. Never reuse an index.
  vsco2.strings.violin-section/
    map.json                       # the compiled instrument (schema below)
    samples/0001.flac …            # 16-bit (24-bit kept only if the source is 24 and the family is piano), mono or stereo, 44.1/48 k
    source/                        # original .sfz / .sf2 + LICENCE + provenance.csv (URL, author, licence, date, sha256, edits)
```
`map.json` (flattened; one record per region):
```json
{ "torg": 1, "id": "vsco2.strings.violin-section", "name": "Violin Section", "family": "violin", "category": "Strings",
  "credit": "Versilian Studios, CC0", "polyMax": 32, "hasNoise": false, "hasRelease": false,
  "artics": ["Sustain", "Staccato", "Pizzicato"],
  "regions": [ { "a": 0, "kind": "attack|release|noise", "smp": 12, "lk": 55, "hk": 57, "lv": 1, "hv": 63,
                 "xfLo": 48, "xfHi": 63, "root": 56, "cents": -3.1, "gainDb": -1.8, "gainNorm": 1.12, "pan": 0,
                 "start": 0, "end": 190112, "onset": 211, "loop": "sustain", "ls": 60210, "le": 181004, "xf": 2205,
                 "tailLs": 0, "tailLe": 0, "rr": [0, 4], "rand": [0, 1], "grp": 0, "offBy": 0, "offMode": "normal",
                 "env": {"a": 0.005, "h": 0, "d": 0, "s": 1, "r": 0.35}, "rtDecay": 0, "velCurve": [[0, 0], [127, 1]] } ] }
```
Everything else in the ARIA tail (`lfoNN_*`, `egNN_*`, `eqN_*`, `*_onccN`) is **dropped at compile time**. Its job belongs to Terrain's mod matrix and FX. `label_ccN`/`set_ccN` defaults are **evaluated** at compile time to pick the default mix, for example Salamander's release-noise level.

### 4.2 Player: write our own, or embed sfizz / TinySoundFont?

| | **sfizz** (BSD-2) | **TinySoundFont** (MIT) | **Own `OrganicsEngine`** ✅ |
|---|---|---|---|
| Size | 3.2 MB C++ + 0.7 MB C source; Abseil, KISS FFT, pugixml, dr_libs ([GitHub languages](https://github.com/sfztools/sfizz)) | `tsf.h` 2,079 lines, one header | ~900 lines (engine) + ~300 (library/cache) |
| Maintenance | **Archived read-only on 2026-06-21**; last release 1.2.3 (2024-01-14) | Active; "No LLM contributions" policy (use is fine) | Ours |
| CPU | Good. Hermite/Sinc interpolators, 4 background threads, preload 8,192 frames (`Config.h`) | Cheap, but **linear interpolation only**, biquad LPF per voice | Hermite (matches `SampleEngine`), no internal filter (Terrain's filters do it) |
| Fit with Terrain | Brings its own filters, envelopes, EQ, LFO and effect buses, which **duplicate** Terrain's per-voice chain and mod matrix; its voice model does not know Terrain's unison, pool banks or Patcher taps | SF2 only; no release triggers, no loop crossfade, no RR, no choke groups | Built for `renderXxxOsc` (same shape as `renderModalOsc`, `V:8368`) |
| Verdict | Use as a **reference** for opcode semantics (`off_mode`, `rt_decay`, RR rules) | Use its **hydra/zone-merge logic as the reference for the SF2 path in the compiler** (or port the ~400 relevant lines to Python) | **Build** |

**Compiler opcode coverage** (the subset the target libraries use, from the §1 survey, plus the SF2 generator set from `soundfont-research.md` §4.2):
- **Structure:** `<control> <global> <master> <group> <region> <curve>`, `#define`, `#include`, `default_path`, `note_offset`, `octave_offset`, note names.
- **Mapping:** `sample`, `key`, `lokey`, `hikey`, `lovel`, `hivel`, `pitch_keycenter`, `pitch_keytrack`, `tune`, `transpose`.
- **Level:** `volume`, `pan`, `amp_velcurve_N`, `amp_veltrack`, `amp_random`, `group_volume`, `curve_index` (evaluated into `velCurve`).
- **Sample:** `offset`, `offset_random` (evaluated as a Human hint), `end`, `loop_mode`, `loop_start`, `loop_end`, `loop_crossfade`.
- **Variation:** `seq_length`, `seq_position`, `lorand`, `hirand`.
- **Triggers:** `trigger` (`attack|release|first|legato`), `rt_decay`, `delay` (humanise hint).
- **Voicing:** `group`, `off_by`, `off_mode`, `off_time`, `polyphony`, `note_polyphony`.
- **Articulations:** `sw_lokey`, `sw_hikey`, `sw_last`, `sw_default`, `sw_label`.
- **Crossfades and CC:** `locc64`/`hicc64` and `sustain_cc` (pedal variants); `xfin_*`, `xfout_*` over vel/key/CC1 (to Dynamics); `set_ccN`, `label_ccN` (defaults).
- **Envelope:** `ampeg_attack`, `ampeg_hold`, `ampeg_decay`, `ampeg_sustain`, `ampeg_release`, `ampeg_dynamic`.
- **Ignored:** `bend_*` (Terrain has pitch bend); `lfoNN_*`, `egNN_*`, `eqN_*`, `fil*`/`cutoff*`, `*_onccN`, `amplitude_*`.

**SF2:** `keyRange`, `velRange`, `sampleModes`, the address offsets, `overridingRootKey`, `coarseTune`, `fineTune`, `scaleTuning`, `initialAttenuation`, `pan`, `volEnv*`, `exclusiveClass`, stereo `sampleLink`. SF3 is decoded with `stb_vorbis` at compile time (lossy source, flagged in the provenance).

---

## 5. CPU and memory design

### 5.1 CPU targets (measured on the installed AU with `Tests/au_cpu_profile.cpp`, 512 frames @ 48 kHz)

tp32 baseline, 4-note chord: **WT 352 µs · Samp 318 · Modal 424 · Harmonic 604 · Geode 812**. Per sounding note ≈ 50 µs, and most of that is the per-voice filter, envelopes and mod application, not the engine.

| Metric | Target |
|---|---|
| Engine core per region, per 512 frames (stereo Hermite + gain ramp + tilt) | **≤ 4 µs** (about 8 ns/frame). That is ≤ 0.35× a WT unison voice core. |
| 4-note chord, unison 1, one Organic osc vs the same patch on WT | **≤ 352 µs** (not slower than WT) |
| 8-note chord, unison 7 ("ensemble") | ≤ WT at unison 7 |
| Worst case per note while crossfading | 2 layer regions + 1 release + 1 noise = 4 readers, bounded by the 48-region per-osc cap |
| Dice price | `ENGW[7] = 0.5` |
| Idle (instrument loaded, no notes) | 0 µs (the engine returns before any work when it has no regions) |
| Unused (no oscillator on Organic) | **bit-identical output**, 0 bytes of sample memory, no thread |

**How the cost stays low:**
- Block processing. Per block and per region: compute `ratio`, the gain ramp endpoints and the tilt coefficients once. The inner loop is interpolate → gain → accumulate.
- int16 storage, converted in the interpolator (half the memory bandwidth of float).
- **No per-voice filter inside the engine.** Tone is a 1-pole shelf; the real filtering is Terrain's. **No per-sample transcendentals** (the tp36 roadmap lesson).
- Stereo samples read both channels from one interleaved array, so there is one index computation.
- Regions below −90 dBFS after release are retired immediately. The **release-tail law from tp36** applies: a finished release never keeps a voice alive.

### 5.2 Memory

**Day 1: RAM-resident int16, with an authoring budget.**
- **Budget per instrument:** typical ≤ 48 MB. Flagship piano ≤ 160 MB (8 of Salamander's 16 layers, every minor third, tails truncated at −60 dB relative with a 200 ms fade, or cut at the tail loop).
- **The compiler enforces the budget** and reports `sizeMB` into `index.json`.
- Load on a background thread (the `SampleLoader` worker pattern). The audio thread swaps an `std::shared_ptr<const OrganicInstrument>` atomically, exactly like `SampleBuffer.h`. While loading, the oscillator plays silence and the UI shows a thin progress line.
- **One shared cache** keyed by instrument id, refcounted by *oscillator slot*, not by voice. Four oscillators playing the same piano hold one copy. Release happens 5 s after the refcount reaches 0 (tp63: "released when idle").
- Library index (`index.json`) is about 50 KB, loaded when the browser first opens.
- **Library location:** `terrainDataDir()/Organics/` (`userApplicationDataDirectory` + Terrain, `E:167-237`; on Windows that is `%APPDATA%`). Settings has a "Library location…" override for an external SSD. The factory installer writes there. **It is not a bundle resource** (the wavetable bundle approach, `CMakeLists.txt:58-65`, does not scale to GBs).

**v1.1: direct-from-disk streaming, flagship pianos only.** This follows Kontakt DFD (default preload 60 KB, lower on SSD, [ADSR on DFD](https://www.adsrsounds.com/kontakt-tutorials/how-to-use-and-optimize-kontakt-dfd/)) and sfizz (`preloadSize 8192` frames, `numBackgroundThreads 4`, `loadInRam false`, sfizz `Config.h`).
- Preload the first **8,192 frames** of every sample: 480 samples × 8,192 × 4 B ≈ **16 MB** instead of 160 MB.
- A 2-thread streamer fills a 65,536-frame ring per streaming region.
- The reader API is designed **now** as `const int16* framesAt (pos, n)` plus a `ready` watermark, so the voice never learns whether the data is resident or streamed.
- On an underrun, the voice fades out in 5 ms (never clicks) and the underrun is counted in the CPU beacon.

**Estimated factory library on disk:** 2–3 GB as FLAC (about 55 % of 16-bit WAV). This fits the "~3–4 GB after trimming" figure in `soundfont-research.md` §3.

### 5.3 Aliasing and pitch range
- The compiler guarantees that zones span **≤ 3 semitones around the root, biased so a zone is pitched *down* more than up** (root at the top of the zone where the source allows). Most sources are sampled every whole tone or minor third, which gives at most +1.5 st upward in the default mapping.
- Hermite on 44.1/48 k material that is repitched up by ≤ 2 st keeps aliases below −60 dB for piano, string and wind spectra, because their content above 16 kHz is low. **Body** (up to +7 st upward in total) and large upward OCT/SEMI transposition are where artefacts would appear. Day 1 clamps them. v1.1 adds lazily built 2× decimated copies for any region asked to play more than +5 st, the wavetable-mipmap idea applied to samples, at +50 % memory for only those regions.
- Offline bounce uses Sinc8 regardless.

### 5.4 Interaction with the 8 oscillators and unison
- An oscillator is one instrument. Layer a piano on A and strings on B with different Body/Tone settings: that is the "Terrain way" to build hybrids. **All eight oscillators may run Organic.** Memory is shared per instrument, and CPU is per region.
- Unison becomes **Ensemble** (§2.5). Plain detuned copies of a piano sound like a broken chorus, and the engine never produces that by default: Detune is scaled to 0.25× and each player gets its own RR sample and timing.
- Bank B (E–H) needs no extra work, because the gather reads through `rawParamB` (`P:11179`) and the parameters clone (`P:7674`).

---

## 6. The visualiser

### 6.1 Approach: the "line-kit" (parametric 3D primitives → SVG)

| Option | Look | Cost to build | Runtime cost | Reactivity |
|---|---|---|---|---|
| Hand-drawn isometric SVG per family | Can be beautiful, **if** an illustrator draws all 22; agent-drawn freehand paths look lumpy and inconsistent | High (art) | Free | Needs named sub-paths; OK |
| A real wireframe renderer (low-poly meshes, z-buffer hidden-line removal on canvas) | Reads as "CAD", with triangle diagonals and noisy silhouettes ([silhouette/crease extraction](https://visualizationlibrary.org/docs/2.0/html/pag_guide_edge_rendering.html) is needed to look designed) | High (meshes plus an edge-classification renderer) | Per-frame projection work | Easy |
| **Line-kit** ✅ | Designed, consistent, Teenage-Engineering-like (clean primitives, one stroke weight, real perspective) | **Low: a family is 20–60 lines of JS** | Projected **once** and cached as SVG path strings; only the reactive parts are re-projected on note events | Built in: every primitive has an id |

**The kit** (`Source/ui/public/index.html`, one IIFE of about 350 lines, `window.__orgLineKit`):
- **Primitives** (all in instrument-local 3D units):
  - `box(w, h, d, bevel)`
  - `extrude(outline2D, depth)`: violin, guitar and harp bodies, drawn as a spline outline plus depth
  - `lathe(profile, segments=14)`: bells, horn bell, timpani, drum shells
  - `tube(path3D, r)`: sax neck, horn and trumpet tubing, drawn as two offset silhouette lines plus a few rings, not a mesh
  - `strings(n, from, to)`
  - `keyRow(n, whiteW)`
  - `bars(n)`: mallets
  - `pipes(n)`: organ
  - `holes(n)` / `valves(n)`
- **Projection:** a fixed ¾ view (yaw −28°, pitch 18°) with weak perspective. On pointer hover the view eases ±6° of yaw over 300 ms (a free "3D" feel), then rests. Hover is a gesture, so it follows the fb591 gesture-clock law.
- **Hidden lines:** primitives are drawn back to front. Each closed face is filled with the display's background colour, then stroked, so near geometry occludes far lines (painter's algorithm, no z-buffer). Style: white, `stroke-width 1`, `vector-effect: non-scaling-stroke`, rounded joins, 0.55 opacity at rest.
- **Reactive layer** (driven by the existing per-osc viz feed plus a new `organicViz` payload: active notes, per-note level, sustain pedal):
  - piano: the **pressed keys dip 1.5 units and turn purple**; the hammer rail line flickers
  - strings (violin, cello, guitar, harp, bass): the string nearest the pitch **vibrates** (the path is re-emitted with a decaying sine displacement for about 400 ms)
  - winds: tone holes fill according to a pitch → fingering LUT (approximate)
  - brass: valves press, and the bell rim pulses outward
  - mallets and bells: the struck bar or tube lights up and rings (concentric strokes fade)
  - organ: the played pipe lights
  - choir: arcs rise from the figure
  - drums: the head flexes
  - Overall stroke opacity follows the oscillator level (0.55 → 0.95).
- **Budget:** with no events there are **0 frames** (the rest law of fb581/fb590). While notes animate, the redraw is capped at 30 fps and touches only the reactive `<path>`s. The static body is one cached `<path d>`.
- **Where:** the engine display area (the same geometry as the other engines: picture h = 65 at y = 81 on the unison page, per fb590; full height on page 1). An `.organic-view` sibling of `.sample-view` carries it. **Every engine keeps its own picture on the unison page** (fb590 bug A: do not let `.uni-page` hide it).

### 6.2 The 22 families (covering the cleared library)

| # | Family id | Drawing | Library sources |
|---|---|---|---|
| 1 | `grand` | grand piano, lid up, key row | Salamander, VCSL Steinway B/Kawai, Headroom, Osiris |
| 2 | `upright` | upright piano, key row | VCSL Knight/Yamaha, VSCO upright, FreePats Upright KW |
| 3 | `epiano` | EP case on legs (tine/reed) | Greg Sullivan Wurli EP200/CP80/Pianet T, FreePats FM EP |
| 4 | `harpsichord` | harpsichord, lid up | VCSL English/Flemish/French/Italian |
| 5 | `pipe-organ` | pipe rank plus console | VCSL Pipe/Renaissance Organ, VSCO Organ |
| 6 | `drawbar-organ` | tonewheel cabinet with drawbars (drawbars move with velocity) | FreePats drawbar/rock organs |
| 7 | `clav` | clav-style keyboard box (built from our own content or the TX81Z set) | TX81Z / future |
| 8 | `violin` | violin plus bow, 4 strings | VSCO violin/viola sections, solo violin |
| 9 | `cello` | cello/contrabass on endpin | VSCO cello, solo contrabass, bigcat cello |
| 10 | `harp` | concert harp frame, strings | VCSL Concert/Folk Harp, VSCO Harp |
| 11 | `guitar` | acoustic guitar | FreePats nylon, Karoryfer Emily |
| 12 | `e-guitar` | hollowbody electric | Karoryfer Shiny, Black-and-Green, FSBS cleans |
| 13 | `bass` | electric bass (upright uses `cello`) | Karoryfer Meatbass/Growlybass/…, Smolken |
| 14 | `zither` | psaltery / dan tranh (flat board, many strings) | VCSL psaltery, dan tranh, strumstick |
| 15 | `flute` | flute / piccolo / recorder (a tube with holes) | VSCO flute/piccolo, VCSL recorders |
| 16 | `reed` | clarinet / oboe / bassoon | VSCO woodwinds |
| 17 | `sax` | saxophone | Karoryfer Weresax/Bear Sax, VCSL tenor, MTG SoloSax |
| 18 | `trumpet` | trumpet (valves) | VSCO trumpet |
| 19 | `horn` | French horn (coiled tube, bell) | VSCO F horn |
| 20 | `low-brass` | trombone / tuba | VSCO trombone/tuba, Karoryfer War Tuba |
| 21 | `mallets` | marimba / vibes / xylo / glock (bars on a frame) | VCSL mallets, VSCO percussion |
| 22 | `bells` | tubular bells / chimes / handbells | VCSL tubular bells, hand chimes, Nepalese bells |
| (+) | `choir`, `drums`, `ocarina` | a figure with arcs / a drum shell / an ocarina | a future choir (a gap in the licence research), Karoryfer kits, VCSL ocarina |

Day 1 builds families 1, 3, 8, 11, 15 and 21 (these cover the day-1 content, §7). The rest follow at about 20 minutes each.

---

## 7. Terrain integration

### 7.1 Engine slot
- `V:169`: `enum class Engine { …, MODAL = 6, ORGANIC = 7 }`. Update the clamps `jlimit(0,6,…)` → 7 at `V:1154-1158`, `V:1448-1452` and `V:2883-2884`.
- **Choice param:** `StringArray {"WT","SAMP","GRAN","SPEC","FM","HARM","MODAL","ORGANIC","R8","R9","R10","R11"}` at `P:4161`, `4832`, `5028`, `5224`. Reserving slots 8–11 takes the host-automation shift **once** (RACK LAW C, `P:109-118`). Saved presets store the index and are safe (`PresetCarries.h:113-121`). The UI never offers R8–R11. `__synChoiceCount` (`H:19715`) normalises by the backend cardinality.
- **Every hard-coded `/6` and `…,6` in JS must become `/(card−1)`:** `H:19881-19904`, `20271`, `20273`, `20364`, `43760`, `43801`, and the four `<select class="engine-select">` lists (`H:9000`, `9343`, `9710`, `10053`), plus the hidden select at `H:15907`.
- **Tests with literals:** `Tests/win_blk_cpu.cpp:204` (MODAL as 1.0), `Tests/fm_null_dump.cpp:212` (`4/6`), `Tests/dice_rules_gate.js:277` (`…, 7`), `Tests/osc_view_gate.js` ("seven engines"), `Tests/au_cpu_profile.cpp:388-395` (`kEng[7]`).
- `PresetCarries.h:95-151`: add `kEngOrganic = 7`. It is **not** a sample-slot engine (`slotPlaysSample` stays false: an Organic osc never saves `oscSamplePath`/`oscAsset`). Update the static_asserts at `P:18517-18529`.

### 7.2 Parameters (all `SYN_OSC_<X>_ORG_*`; A–D declared, E–H hand-appended to `ParameterIDs.hpp` and then `scripts/gen_osc_bank_ids.py` regenerates `OscBankIds.h`)

| ID suffix | Type | Range | Default | Mod dest |
|---|---|---|---|---|
| `ORG_INST` | AudioParameterInt | 0..4095 (index into the append-only `ids.json`) | 0 (none) | no (automatable; the switch is async) |
| `ORG_ARTIC` | AudioParameterInt | 0..7 | 0 | no |
| `ORG_DYNAMICS` | float | 0..1 (bipolar) | 0.5 | ✅ |
| `ORG_TONE` | float | 0..1 (bipolar) | 0.5 | ✅ |
| `ORG_BODY` | float | 0..1 (bipolar) | 0.5 | ✅ |
| `ORG_ATTACK` | float | 0..1 (bipolar) | 0.5 | ✅ |
| `ORG_HUMAN` | float | 0..1 | 0.25 | ✅ |
| `ORG_RELEASE` | float | 0..1 | 0.5 | ✅ |
| `ORG_NOISE` | float | 0..1 | 0.5 | ✅ |
| `ORG_SUSTAIN` | float | 0..1 | 0 | ✅ |
| `ORG_VELOCITY` | float | 0..1 | 0.75 | ✅ |
| `ORG_IMAGE` | float | 0..1 (display 0..1.5) | 0.667 | ✅ |

INST and ARTIC are ints, not choices, because a choice's cardinality is frozen at birth and the library grows (the fb342 law).

**Layout:** an `addOrganicOsc` lambda next to `addModalOsc` (`P:5788-5815`). The gather goes after the MODAL gather (`P:12079-12113`) into `tw::OrganicParams orgP[4]`, pushed like `setModalParamsA..D` (`P:12506`). The lazy arm is `prepareOrganicEnginesIfNeeded`, the same shape as `P:3211-3222`: it scans `kOsc_ENGINE` for 7, allocates the engines, and asks the library cache for the instrument.

**Mod destinations: 80 new.** The destination space below 1890 is frozen and the bank-2 mirror only covers the legacy range (`SynthModConfig.h:261-309`). So the new block is **appended after `ShaperDepthEnd`**: `OrganicBase = 5272`, dest = `OrganicBase + osc(0..7)·10 + knob(0..9)`, `NumDests` 5272 → **5352**. The static_asserts at `SynthModConfig.h:285-309` and the JS mirror at `H:16313` must be updated. KNOBDEST (`H:35359-35385`) gets an `ORG_*` family whose E–H entries are **explicit**, not `+OSCBANK2_BASE`. Add the new rows to `isOscLetteredDest` (`SynthModConfig.h:317`) so they re-letter correctly. (Harmonic is the precedent with dests. Modal has none, a gap worth closing separately.)

### 7.3 Preset format
- **Parameters** save as usual.
- **The instrument is saved by stable string id** in the state XML: `<ORGANICS><OSC slot="0" id="salamander.grand.v3" rev="1"/>…</ORGANICS>`, written next to `oscSamplePath{i}` (`P:18838`).
- **On load:**
  - The string wins. `ORG_INST` is re-derived from `ids.json`.
  - An unknown id (library not installed) falls back to the family default ("grand" → the first installed grand). If nothing matches, the oscillator is silent and the UI shows the "Instrument not installed: Salamander Grand" pill. **Never a crash, never a wrong instrument without saying so.**
  - `rev` lets a re-compiled instrument keep old presets close (the compiler never renumbers `ids.json`, and only bumps `rev` for audible changes).

### 7.4 Browser
Reuse `window.openTwoPaneBrowser(ev, {cats, openCat, searchPlaceholder})` (`H:42211`, the same component as the filter, noise and wavetable browsers). The left pane lists categories: **Keys · Organs · Strings · Plucked · Winds · Brass · Mallets & Bells · Choir & Voice · Percussion** (purple category rows, per tp102). The right pane lists instruments with size, credit, and a ▶ **audition** button (a middle-C velocity-90 one-shot rendered offline by the compiler as `preview.flac`). No live voice is needed, so the browser costs nothing while the synth plays. Mouse-wheel over the instrument pill steps through the instruments, like Serum's preset hover-wheel.

### 7.5 Patcher
Nothing engine-specific is needed. Oscillator nodes are `kind==='osc'` and `adoptOsc` (`H:45554`) moves the live DOM. **Watch-outs from tp40:**
- E–H are clones of B minted at parse time (`H:10402`). Every per-letter wiring for the Organic view (instrument pill, browser, visualiser mount) must be delegated from a container that survives adoption (`#tp-page`), or replicated for e–h. Never use `getElementById('osc-a-…')` only.
- Direct taps (`<device>_TAPS`, tp41) work unchanged, because the engine writes into the same per-osc block buffers as Modal.

### 7.6 Dice / randomiser
- `ENGW[7] = 0.5` (`H:46301`).
- Add an `organic` boolean to the engine pick (`H:46459-46493`) with probability 0.12. It rolls a **random installed instrument by category**, then rolls Dynamics, Tone and Body within ±0.25 of neutral, Human 0.1–0.5, Image 0.4–1.0, and leaves Release/Noise/Sustain/Velocity at defaults with 70 % probability (a musical, not chaotic, default).
- **Never roll** an instrument whose load exceeds 160 MB while `cpuCap` is on.
- `choiceNorm` already asks the backend for the cardinality (`H:45364`).

### 7.7 Windows
- The library path is `%APPDATA%\…\Terrain\Organics` via the same `terrainDataDir()`.
- All file paths are `juce::File`, UTF-8.
- FLAC decoding is JUCE's built-in reader (already proven on Windows: the wavetable FLACs load).
- MSVC: no raw string literals over 16 KB (the fb636 C2026 lesson); the kit is JS in `index.html`.
- **No large stack arrays:** the `OrganicInstrument` region table lives on the heap (the fb636 stack-overflow lesson).
- The CI Windows job must build `organics_engine_test`.

---

## 8. One-day build plan (4 agents, 4 worktrees)

### Hour 0 (lead, 30 min, before the fork): freeze the contracts
1. The `.torg` v1 schema and folder layout (§4.1). Commit a **5-region synthetic fixture** `Tests/fixtures/organics/test.sine/`, where each region is a sine at a distinct frequency so tests can tell which region sounded.
2. `Source/organics/OrganicsApi.h`: the only header the other agents include:
   ```cpp
   namespace tw {
   struct OrganicParams { float dyn=0, tone=0, body=0, attack=0, human=.25f, release=.5f, noise=.5f, sustain=0, velo=.75f, image=1; int artic=0; };
   class OrganicInstrument;                    // opaque, immutable, shared_ptr<const>
   class OrganicsLibrary {                     // message-thread API
   public: static OrganicsLibrary& get();
     juce::var index();                        // index.json
     void request (const juce::String& id, std::function<void(std::shared_ptr<const OrganicInstrument>)>);
     int  idToIndex (const juce::String&) const; juce::String indexToId (int) const; };
   class OrganicEngine {                       // one per osc slot per voice (x16 unison siblings inside)
   public: void prepare (double sr, int maxBlock);
     void setInstrument (std::shared_ptr<const OrganicInstrument>) noexcept;   // audio thread, lock-free swap
     void noteOn (int note, float vel, int players, const float* detuneCents, uint32_t seed) noexcept;
     void noteOff (bool pedalDown) noexcept; void pedal (bool down) noexcept;
     void render (const OrganicParams&, float pitchCents, float* L, float* R, int n) noexcept;  // ADDs into L/R
     bool isActive() const noexcept; float readLevel() const noexcept; };
   }
   ```
3. The parameter ID list (§7.2), `OrganicBase = 5272`, and the native names `organicsIndex()`, `organicsSetInstrument(osc, id)`, and the event `onOrganicViz({osc, notes:[{n,lvl}], pedal})`.

### The four agents

| Agent | Owns (exclusive files) | Delivers | Proves it with |
|---|---|---|---|
| **A: Compiler & content** | `Tools/organics/**` (torgc.py, sfz.py, sf2.py, analyse.py), `Resources/Organics/**`, `Tests/organics_compile_test.py` | SFZ/SF2 → `.torg`: flattening, `#include`/`#define`, the opcode subset (§4.2), keyswitches → artics, RR, release/noise tagging, **onset detection**, **tail-loop search** (autocorrelation), RMS `gainNorm`, budget trimming, preview render, provenance CSV, append-only `ids.json`. **Six day-1 instruments:** Salamander Grand (8 layers, CC-BY, plain FLAC), Greg Sullivan Wurli EP200 (CC-BY), VSCO 2 CE Violin Section (CC0), FreePats Nylon Guitar (CC0), VSCO 2 CE Flute (CC0), VCSL Vibraphone (CC0) | The compile test re-parses each output; zone coverage has no holes over the declared key and velocity range; round-trip of fixture SFZ → torg → expected regions; the budget report; a loop seam in each tail loop with `|Δ| ≤ 1.5 ×` the local median |
| **B: Runtime DSP** | `Source/organics/OrganicsApi.h` (after H0), `OrganicsLibrary.h`, `OrganicEngine.h`, `Source/organics/OrganicEngine_test.cpp` | Everything in §3 "day 1" and §2's DSP: lookup table, Hermite reader (lifted from `SampleEngine.h:499-600`), loop crossfade, velocity/Dynamics crossfade, RR + fake RR, release triggers + `rt_decay`, chokes (5 ms), artics, pedal, noise regions, Tone shelf, Body shift, Attack (onset, fade, softer-layer blend), Human (per-note seed), Sustain tail loop, Image, players (ensemble), a 48-region cap with steal-fade, **Sinc8 when non-realtime**, and the cache with refcount plus a 5 s release. Works against the synthetic fixture only, so it does **not wait for A** | Standalone test binary (listed below) |
| **C: Integration (C++)** | `Source/ParameterIDs.hpp`, `Source/OscBankIds.h` (regenerated), `Source/SynthVoice.h`, `Source/PluginProcessor.cpp/.h`, `Source/PluginEditor.cpp/.h`, `Source/SynthModConfig.h`, `Source/PresetCarries.h`, `CMakeLists.txt`, and the literal fixes in `Tests/*.cpp` | Engine slot 7 plus the 12-choice list, the params (layout lambda and E–H hand-append), gather/push, lazy arm/disarm, `renderOrganicOsc` (the same shape as `renderModalOsc`, `V:8368`) plus every per-osc exclusion site (`V:4279`, `4435-4438`, `4865/5242/5607/5972`, `4910/…`, `4926/…`), `getVizDiag`, mod dests 5272–5351, state save/load of `<ORGANICS>`, the natives, the `onOrganicViz` feed (≤ 15 Hz, only while notes sound), and CMake adding the test target. It **builds against B's `OrganicsApi.h` from H0**, with a stub `.cpp` until B merges | The AU gates (listed below) |
| **D: UI (JS/CSS)** | `Source/ui/public/index.html` only, plus `Tests/_org_*.js` | The engine menu entry, `.engine-organic` view, `.organic-knob-wrap` (`.organic-pg1`/`.organic-pg2` + `.organic-arrow`, registered in `WRAP_OF` `H:19978` and the click list `H:20058`), bipolar ring rendering for Dynamics/Tone/Body/Attack, the instrument pill + two-pane browser + audition, the artic select, the greyed Noise state, the **line-kit** plus families 1, 3, 8, 11, 15 and 21 with reactive layers, the unison page keeping the picture (fb590), `__setSynParam` routing for `_ORG_` (`H:21746-21813`), KNOBDEST `ORG_*`, dice `ENGW[7]` and roll, every `/6` fix (§7.1), and the E–H clone wiring. It uses a **mock native layer** (`window.__orgMock`) until C merges | Headless page gates (listed below) |

**Order and merges:**
- **H0–0.5:** the lead freezes the contracts (above) and creates the 4 worktrees from `3e5e124` plus the contracts commit.
- **H0.5–H6:** all four work in parallel.
- **Merge 1 (≈H5):** B into the integration branch (new files only, no conflicts possible).
- **Merge 2 (≈H6):** C (owns the big shared files, and rebases onto B, replacing its stub).
- **Merge 3 (≈H6.5):** D (index.html only).
- **Merge 4:** A's content (Resources/Tools only).
- **H6.5–H8:** the lead runs the full gate battery on the **installed** AU/VST3/Standalone, fixes integration seams, and does the Windows CI run. Build hygiene is mandatory:
  - Bust the WebUI BinaryData cache.
  - Grep the build log for errors ("a filtered build log hides a failed build").
  - `strings`-verify the embed.

### The tests (each measured, each with a pass line)

| Test | Where | Pass |
|---|---|---|
| **Pitch accuracy per key** | B: `OrganicEngine_test` renders keys 21–108 from a sine fixture (Human = 0). Also on the installed AU with the real Salamander (YIN f0) | \|error\| ≤ **3 cents** for every key; Body ±1 still ≤ 3 cents |
| **Velocity layer switching** | B: a fixture with 4 layers, each a distinct sine frequency; sweep vel 1–127 and Dynamics −1…+1; Goertzel per marker | The right marker per velocity band; in crossfade bands the summed power stays within **±0.5 dB**; no marker from a non-adjacent layer |
| **RR alternation** | B: a `seq_length=4` fixture, 16 repeated notes; random RR, 400 notes | The sequence is exactly 1234·1234·…; random RR has **no immediate repeat** and each RR gets 25 % ± 5 %; with Human = 0 and a fixed seed the output is bit-identical run to run |
| **No clicks at loops, releases or chokes** | B: 10 s looped sustain, 200 note-offs at random phases, and 100 chokes; a high-passed (8 kHz, 4th order) residual | Peak residual at seams, offs and chokes ≤ **−60 dBFS** relative to the note, or ≤ 1.5× the local median \|Δ\| |
| **CPU per voice vs wavetable** | C: `Tests/au_cpu_profile.cpp` gets an `organic` scenario (Salamander, 4-note and 8-note chords, unison 1 and 7) on the installed AU | Organic ≤ WT in the same scenario; engine core ≤ 4 µs per region per 512-frame block (the `TERRAIN_PROFILE` split) |
| **Memory per instrument** | C: an `au_lazy_memory`-style harness (`malloc size_in_use`, the tp64 method) loads each day-1 instrument, then unloads | Loaded ≤ `sizeMB` in the index + 5 %; back to baseline ± 2 MB within **6 s** of the last osc leaving Organic |
| **Bit-identical when unused** | C: `Tests/pool_identity.cpp` + `TERRAIN_DETERMINISTIC` frozen-bank run (`abS.sh`), all 52 factory presets, before vs after | **0 differing samples** on all 52; no Organics thread started (checked via the thread list) |
| **State round-trip** | C: save a preset with Organic on A and F, reload into a fresh instance; also a missing-library load | Same instrument id, artic and knobs; missing → silent osc + "not installed" pill, no crash |
| **UI gates** | D: `_org_view_gate.js` (all 8 oscs incl. E–H clones on the Patcher canvas; page 1 → 2 → unison cycle; the picture is present on the unison page); `_org_viz_rest_gate.js` | Every knob writes its param; the arrow cycles 1 → 2 → 3; **0 painter frames at rest**, ≤ 30 fps while notes sound |
| **Existing gates stay green** | lead: `dice_rules_gate.js`, `osc_view_gate.js`, `win_blk_cpu`, `fm_null_dump`, `au_osc_pool`, `au_flow_route` after the literal updates | All pass |

---

## 9. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Growing the engine choice shifts host automation lanes of `SYN_OSC_x_ENGINE` | Certain (once) | Reserve 12 slots now; note it in the changelog. Engine lanes are rarely automated. Saved presets are unaffected (they store the index). |
| Tail and lookup cost when many players × layers × release overlap (the tp36 "tails × per-voice cost" lesson) | Medium | The 48-region per-osc cap with steal-fade; releases retire at −90 dBFS; `enforceVoiceCap` still applies at the voice level. |
| RAM on the flagship piano (160 MB × several instances) | Medium | A shared cache (one copy per instrument); v1.1 streaming takes it to ~16 MB resident; the dice avoid > 160 MB instruments under the CPU cap. |
| The one-day schedule slips on C (the big shared files) | Medium | C starts from the frozen API with a stub; B and D never touch C's files; the lead takes the literal-fix list (§7.1) if C runs late. |
| Agent-authored line art looks amateur | Medium | The line-kit makes it *geometry*, not illustration; one stroke weight and one projection; the lead reviews each family against a TE/Apple reference sheet. A bad family ships as a clean generic "case" outline rather than an ugly drawing. |
| CC-BY content accidentally packed or encrypted | Low | `.torg` keeps plain FLAC and `source/LICENCE`; the compiler refuses an `encrypt` flag for any `licence != CC0/MIT`; credits are generated into About → Licences from `index.json`. |
| Aliasing on Body+ or high transposition | Low–medium | Day-1 clamp (+7 st in total); Sinc8 offline; v1.1 lazy mips. |
| Pitch errors from sources with poor tuning (VCSL is "lightly processed") | Medium | The compiler measures f0 per region and writes corrective `cents`; the pitch gate runs on the **real** instruments, not only fixtures. |
| sfizz and TSF are only references, so an opcode might be misread | Low | The compile test round-trips the fixture SFZ; spot-check Salamander and Karoryfer against sfizz rendering (sfizz is still usable as an offline oracle even though it is archived). |
| The Patcher adopts the DOM and E–H clones miss wiring (tp40 class) | Medium | D's gate runs on the Patcher canvas for all 8 oscs; wiring is delegated from `#tp-page`. |
| Choir and Rhodes are licence gaps (`soundfont-research.md` §3) | Known | Out of day-1 scope; the `choir` family is drawn but has no content until recorded or licensed. |

---

### Sources
- Serum 2: [Xfer product page](https://xferrecords.com/products/serum-2) · [Dubspot review](https://blog.dubspot.com/xfer-records-releases-serum-2) · [Sonic Weaponry breakdown](https://sonic-weaponry.com/blogs/free-production-tutorials-and-resources/serum-2-released) · [EDMProd guide](https://www.edmprod.com/serum-2-guide/) · [monosounds: Serum 2 oscillator types](https://monosounds.studio/serum-2-oscillator-types/)
- Spitfire LABS: [MusicRadar](https://www.musicradar.com/news/fantastic-free-plugins-spitfire-audio-labs) · [9to5Mac](https://9to5mac.com/2020/04/26/free-instrument-library-labs/)
- Arturia Augmented: [product page](https://www.arturia.com/products/software-instruments/augmented/strings) · [KVR](https://www.kvraudio.com/product/augmented-strings-by-arturia) · [Engadget](https://www.engadget.com/arturia-augmented-strings-synth-vst-150018015.html)
- NI Noire: [Sound On Sound review](https://www.soundonsound.com/reviews/native-instruments-noire)
- Keyscape: [Feel](https://support.spectrasonics.net/manual/Keyscape/11/en/topic/feel) · [Noise](https://support.spectrasonics.net/manual/Keyscape/11/en/topic/noise) · [Pedal](https://support.spectrasonics.net/manual/Keyscape/11/en/topic/pedal)
- Decent Sampler / Pianobook: [decentsamples](https://www.decentsamples.com/product/reverb-tank-violin/) · [Pianobook: Decent Sampler](https://www.pianobook.co.uk/sampler/decent-sampler/)
- SFZ spec: [sfzformat opcodes](https://sfzformat.com/opcodes/) · [rt_decay](https://sfzformat.com/opcodes/rt_decay/) · [trigger](https://sfzformat.com/opcodes/trigger/) · [amp_velcurve_N](https://sfzformat.com/opcodes/amp_velcurve_N/) · [sample_quality](https://sfzformat.com/opcodes/sample_quality/) · [loop_crossfade](https://sfzformat.com/opcodes/loop_crossfade/)
- SF2: [SoundFont 2.04 spec (PDF)](http://www.synthfont.com/sfspec24.pdf)
- sfizz: [repo (archived 2026-06-21)](https://github.com/sfztools/sfizz) · `src/sfizz/Config.h` (preloadSize 8192, numBackgroundThreads 4, loadInRam false) · `src/sfizz/Interpolators.h` (Hermite3 … Sinc60) · [releases](https://github.com/sfztools/sfizz/releases)
- TinySoundFont: [repo](https://github.com/schellingb/TinySoundFont); `tsf.h` (2,079 lines, linear interpolation at lines 1294–1339, loop modes at line 385, SF3 via stb_vorbis at lines 867–977)
- Streaming: [ADSR: Kontakt DFD](https://www.adsrsounds.com/kontakt-tutorials/how-to-use-and-optimize-kontakt-dfd/) · [Spitfire: reducing RAM with SSDs](https://spitfireaudio.zendesk.com/hc/en-us/articles/360020762793-Reducing-RAM-usage-when-using-SSDs) · [VI-Control preload threads](https://vi-control.net/community/threads/kontakts-preload-buffer-size.102822/)
- Interpolation: [KVR: highest-quality real-time interpolation](https://www.kvraudio.com/forum/viewtopic.php?t=501053) · [KVR: sinc interpolation](https://www.kvraudio.com/forum/viewtopic.php?t=345011)
- Fake round robin: [VI-Control](https://vi-control.net/community/threads/how-to-create-round-robin-with-a-single-sample.141296/) · [KVR RR simulator](https://www.kvraudio.com/forum/viewtopic.php?t=555002) · [Decent Sampler Q&A](https://www.decentsamples.com/qa/292/could-we-have-a-way-to-fake-round-robins-please)
- Line rendering: [edge / silhouette rendering](https://visualizationlibrary.org/docs/2.0/html/pag_guide_edge_rendering.html)
- Opcode survey: every `.sfz`/`.sfzh` from `sfzinstruments/{SalamanderGrandPiano, BengtNilsson.HeadroomPiano, GregSullivan.E-Pianos, Osiris_Piano, karoryfer.meatbass, karoryfer.weresax, karoryfer.emilyguitar}`, fetched 2026-09-24 (23,297 regions, 159 opcode forms)
- Internal: `.ideas/soundfont-research.md` · memory tp20 (pool), tp32/tp36 (CPU), tp40/tp41 (Patcher), fb590/fb591 (pictures, rest), fb441 (perceptual-delta recompute), tp63 (lazy release)
