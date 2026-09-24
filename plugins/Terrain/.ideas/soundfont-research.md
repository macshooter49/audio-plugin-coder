# SoundFont / SFZ Factory Library — Licence Research + Multi-Sampler Engine Notes

**Project:** Terrain (Waves Crate) — multi-sampler engine playing SF2 / SF3 / SFZ on Terrain's oscillators
**Base:** `feature/terrain-instrument` @ 661c141
**Date:** 2026-09-23
**Goal:** a factory library of good-sounding instruments that ships inside the paid plugin (a few GB is fine).

> **Not legal advice.** Every licence below was read from the source page or the licence file (links given), not taken from blog summaries. Before shipping, save a dated copy of each licence page into `Resources/Licenses/` and keep a per-file provenance log (source URL, licence, date fetched, hash).

---

## 0. The test we applied

Only one question counts: **may we redistribute the sample files themselves inside a product we sell?**
"You may use these sounds in your music, commercially" is a **different permission** and does **not** qualify. Almost every "royalty-free" freebie only gives the second one.

| Licence | Can the samples ship inside a paid plugin? | Conditions |
|---|---|---|
| **CC0 1.0 / Unlicense / explicit public-domain dedication** | **Yes** | None. You may pack, encrypt, or convert the files. |
| **MIT** (e.g. FluidR3 / MuseScore General) | **Yes** | Keep the copyright + licence notice in the docs/About screen. |
| **CC-BY 3.0 / 4.0** | **Yes, with conditions** | (1) Credit the author in the docs/About screen. (2) **No DRM that stops users using the CC-BY files under CC-BY**: CC-BY 4.0 §2(a)(5)(B) and CC-BY 3.0 §4(a) forbid "effective technological measures". Ship these as plain FLAC/WAV, not inside an encrypted blob, and don't let the EULA restrict them. |
| CC-BY-SA | Risky | Share-alike applies to any adaptation we make (edited or looped samples). Avoid for the factory library. |
| CC Sampling Plus 1.0 | **No** | Only non-commercial distribution of the whole work is allowed. |
| CC-BY-NC, "free for your music", "no repackaging", GPL sample sets | **No** | — |

---

## 1. SHORTLIST — CLEARED FOR COMMERCIAL REDISTRIBUTION (ranked)

Ranked by sound quality × usefulness for Terrain × how clean the licence is.

### Tier A — CC0 (no conditions; can be encrypted or repacked)

**1. VSCO 2 CE — Versilian Studios Chamber Orchestra 2, Community Edition** ⭐ the orchestral backbone
- **Licence:** CC0-1.0 (repo licence). https://github.com/sgossner/VSCO-2-CE
- **Covers:** *Strings:* Violin Section, Viola Section, Cello Section, Solo Violin, Solo Contrabass, Harp. *Brass:* F Horn, Trumpet, Tenor Trombone, "OldTrombone", Tuba. *Woodwinds:* Flute, Piccolo, Oboe, Clarinet, Bassoon. *Keys:* Upright Piano, "Upright Nr1", Organ. *Percussion:* Timpani, Glockenspiel, Marimba, Xylophone, plus the VSCO 1 percussion folder.
- **Format / size:** WAV plus SFZ (v1.1.0 release). About 2.3 GB (per the sfzinstruments.github.io orchestra listing).
- **Quality:** Good chamber-section sound. Sustains, staccatos and some vibrato variants. Not a Spitfire-class library (no true legato), but the best CC0 orchestra there is.

**2. VCSL — Versilian Community Sample Library** ⭐ pianos, keys, mallets and percussion
- **Licence:** CC0-1.0. The README says: *"you can do whatever you want with these sounds (even make commercial software), no royalties, no credit, no special terms."* https://github.com/sgossner/VCSL (this is the most explicit grant found anywhere)
- **Covers:** *Pianos:* Grand Piano Steinway B, Grand Piano Kawai (+ legacy), Upright Piano Knight, Upright Piano Yamaha. *Harpsichords:* English, Flemish, French, Italian. *Harps:* Concert Harp, Folk Harp. Dan Tranh, bowed/plucked psaltery, strumstick. *Mallets:* Marimba, Vibraphone, Xylophone, Glockenspiel, Tubular Bells ×2, Tubular Glockenspiel, Hand Chimes, Nepalese hand bells, Balafon. *Organ:* Pipe Organ, Renaissance Organ. *Winds:* baroque recorders (soprano/alto/tenor/bass), ocarinas, Tenor Sax, Saxello, didgeridoo. *Percussion:* large aux set (cymbals, gongs, tambourines, shakers, cajon, claps, woodblock, triangles, cowbells…) plus membranophones. *Synth:* **Yamaha TX81Z** multisamples. Also wine glasses.
- **Format / size:** WAV. An auto-generated SFZ set is on the Releases page. Instruments are about 20–75 MB each; the total size isn't stated.
- **Quality:** Mostly stereo, 44.1/48 kHz, 16/24-bit, lightly processed. Most instruments are sampled every whole tone. Good raw material, but some instruments need our own looping and mapping work.

**3. Karoryfer Samples free libraries** ⭐ basses, guitars, drums, character instruments
- **Licence:** CC0. Their free-samples page says: *"All our free sample libraries are under a Creative Commons Zero license. Older downloads may include a less permissive license, but we changed that and now they are all CC0."* (Exception: **Marie Ork**, a voice bank, is under a different licence, so exclude it.) https://shop.karoryfer.com/pages/free-samples. Each GitHub mirror under github.com/sfzinstruments also shows CC0-1.0.
- **Covers:** *Basses:* Meatbass (double bass), Sneakybass (double-bass pizz), D. Smolken double bass, Growlybass, Swagbass, Fashionbass, Pastabass (Bass VI), Big Little Bass, Black And Blue Basses (2× 5-string), Ergo (electric upright). *Guitars:* Emilyguitar, Shinyguitar (archtop), Black And Green Guitars (2 hollowbody electrics), plus the ganjo. *Drums:* **Big Rusty Drums** (1980s kit), **Virtuosity Drums** (jazz kit, 6 mic positions, with Versilian), Swirly Drums (brushes), Unruly Drums, Frankensnare, The Hat With The Phat, Gogodze Phu I/II. *Winds/brass:* Weresax (tenor), Bear Sax (1926 baritone), War Tuba, Squidpipes. *Strings:* Karoryfer x bigcat Cello. *Synth-like:* **String Cyborgs** (bowed-string synths), **Caveman Cosmonaut** (1983 Unitra organ synth), Cowsynth. Scarypiano (see caveat in §2). 272 Merry Orks (death-metal vocals).
- **Format / size:** SFZ + FLAC/WAV. Sizes are listed per library on the shop page.
- **Quality:** Excellent. Karoryfer is a commercial developer, with deep round robins, many velocity layers and real performances. **The best CC0 bass, guitar and drum material found.**

**4. Osiris Piano** (Versilian Studios + Karoryfer)
- **Licence:** CC0-1.0. https://github.com/sfzinstruments/Osiris_Piano
- **Covers:** Acoustic piano, with clean and "noisy" variants and sustain-pedal variants. Includes a PDF manual.
- **Quality:** Good, but marked beta. A strong second CC0 piano.

**5. FreePats — CC0 items only** (licences are set per instrument, so check each page)
- **CC0 (verified on the pages):** Upright Piano KW (SFZ/SF2; 27–69 MiB full set), Nylon guitar (Spanish classical), FSBS Electric Guitar Clean #1 / Clean #2 (Jazz) / Direct (up to 456 MiB WAV), FM Synthesized Piano #1/#2 (DX7-style EP rendered from Hexter), Drawbar/Percussive/Rock organ (Hammond style, rendered from setBfree), Synth Pads (Choir pad, Sweep, New Age, Bowed), Tenor Sax (derived from VCSL). https://freepats.zenvoid.org/
- **Not CC0 on the same site, so exclude:** FreePats GM Set and GM Percussion Set (GPL-3 with an exception), YDP Grand (CC-BY 3.0), MuldjordKit (CC-BY 4.0; listed under Tier B).
- **Quality:** Mixed. The electric guitars and Upright KW are good. The FM EPs and organs are useful but basic.

### Tier B — attribution / notice required (ship as plain files, credit the author)

**6. Salamander Grand Piano v3** (Alexander Holm) ⭐ best free flagship grand
- **Licence:** **CC-BY 3.0 Unported** (both the sfzinstruments repo and FreePats say so; it is *not* CC0). https://github.com/sfzinstruments/SalamanderGrandPiano · https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html. The repo also asks that derivatives note they are derived from that repo.
- **Covers:** Yamaha C5 grand. 16 velocity layers, 48 kHz / 24-bit.
- **Format / size:** SFZ + FLAC 707 MiB. 48k WAV 1.18 GiB. 44.1k WAV 394 MiB. SF2 296 MiB.
- **Quality:** The best-known free concert grand, and still very usable.

**7. Headroom Piano** (Bengt Nilsson, Yamaha C3)
- **Licence:** CC-BY 4.0. https://github.com/sfzinstruments/BengtNilsson.HeadroomPiano
- 300 samples, 5 velocity layers. About 875 MB as WAV (the repo also quotes 156 MB as FLAC). Uses SFZ v2 + ARIA extensions.

**8. Greg Sullivan E-Pianos** ⭐ the only cleared "real" EP set found
- **Licence:** CC-BY 3.0 Unported. https://github.com/sfzinstruments/GregSullivan.E-Pianos
- **Covers:** **Yamaha CP80** electric grand, **Hohner Pianet T**, **Wurlitzer EP200**. SFZ v2 + FLAC.
- **Quality:** Early-2000s sampling, so layer counts are modest, but these are real instruments. Fills the Wurli, CP and Pianet slots.

**9. Drum kits (CC-BY 4.0):** DrumGizmo **DRSKit** and **MuldjordKit** (2 kicks, 4 toms, snare, hat, crashes, rides, china; velocity layers + random hits); **WilkinsonAudio NakedDrums** (multi-mic, 10 round robins, up to 5 velocity layers). All at https://github.com/orgs/sfzinstruments/repositories (the licence is shown per repo).

**10. MTG SoloSax** (CC-BY 4.0): soprano, alto, tenor and baritone solo saxophones. `sfzinstruments/MTG.SoloSax`.

**11. MuseScore General / FluidR3 GM** (MIT) — only as a GM fallback
- **Licence:** MIT. Copyright notices: Frank Wen (FluidR3, 2000–02), Michael Cowgill (mono conversion), S. Christian Collins (MuseScore_General adaptation 2018–19), Ethan Winer, Michael Schorsch. https://github.com/musescore/MuseScore/blob/master/share/sound/FluidR3Mono_License.md
- **Covers:** full General MIDI (128 programs + drums). SF2/SF3.
- **Caveats:** The sound is dated. The licence is clean on paper, but FluidR3 is a 2000-era compilation, so each sample's origin can't be checked independently. Use it only as an "import any GM file" test set or fallback, not as flagship content.

### Tier C — cleared per file after an audit

**12. Freesound.org — CC0 uploads only** (CC-BY with attribution). The FAQ says of CC0: *"you can do pretty much what you want with the sound. You could even sell the sound… but you can't claim you are the author."* Exclude CC-BY-NC and the old Sampling+ licence. Record the sound ID, uploader and licence **at download time**, because uploaders can change them. https://freesound.org/help/faq/

**13. Discord SFZ GM Bank** (sfzinstruments). The project accepts only CC0, CC-BY or equivalent licences, and each licence is written in the individual .sfz file. There is **no repo-wide licence**, so read each file's header before using it. It pulls from VCSL, VSCO, Salamander, Freesound and MSLP.

---

## 2. SOUNDS GREAT BUT **NOT CLEARED** (don't ship without a written licence)

| Set | What it is | Why it's not cleared (source) |
|---|---|---|
| **Sonatina Symphonic Orchestra** | 1.39 GB full orchestra | **CC Sampling Plus 1.0**: the *whole work* may be distributed only non-commercially, and CC retired the licence in 2011. https://creativecommons.org/licenses/sampling+/1.0/ |
| **Virtual Playing Orchestra** | 618 MB orchestra | Mixes Sonatina (Sampling+) with CC-BY-SA material (Westlund extras, No Budget Orchestra). The author also writes: *"I do not feel it is right to repackage and sell this library in part or in whole for profit."* https://virtualplaying.com/virtual-playing-orchestra/ |
| **Philharmonia Orchestra samples** | Thousands of orchestral one-shots | *"The only restriction is that they must not be sold or made available 'as is' (i.e. as samples or as a sampler instrument)."* That is exactly what we'd be doing. https://philharmonia.co.uk/resources/sound-samples/ |
| **Univ. of Iowa MIS** | Dry chromatic orchestral + piano recordings | *"may be downloaded and used for any projects, without restrictions."* Very permissive, but it never says "redistribute". **Closest to cleared: one email to UIowa EMS for written confirmation would clear it.** Karoryfer Scarypiano (CC0) and parts of VPO rely on this grant, so Scarypiano's clearance depends on it too. https://theremin.music.uiowa.edu/MIS.html |
| **GeneralUser GS v2.0.3** (S. Christian Collins) | Very good GM/GS bank | The licence does say *"Please feel free to use it in your software projects, and to modify the SoundFont bank or its packaging."* But the author also writes that the origin of some samples (taken from free internet sources) is uncertain, and that *"This uncertainty may concern you if you intend to use GeneralUser GS in a commercial software product."* The permission is fine; **the origin of some samples isn't guaranteed.** https://github.com/mrbumpy409/GeneralUser-GS/blob/main/documentation/LICENSE.txt |
| **jRhodes3c / jRhodes3d** (Jeff Learman) | 1977 Rhodes Mk I Stage 73, 5 velocity layers, 90 MB | Samples are **CC BY-NC 4.0**. For commercial use the author says to contact jjlearman@gmail.com. **Best lead for a licensed Rhodes: ask for a commercial licence.** |
| **Maestro Concert Grand** (Mats Helgesson, Yamaha CF-3) | 985 MB, 5 layers | *"may not sell this sound set or any of the samples"*; needs written permission (xo@telia.com). |
| **Splendid Grand Piano** (Akai Steinway) | 256 MB, 4+1 layers | The readme says "public domain samples by AKAI… released as public domain in early 2000". **No primary source for that claim was found.** Unverified public-domain claim = not cleared. |
| **Ivy Audio Piano in 162** | Popular free grand | Free for personal and commercial *music*. Repackaging or modifying it for redistribution is prohibited. |
| **Pianobook (all packs)** | Huge community library | EULA: *"expressly forbids resale or other distribution of the Products or their derivatives… reformatted for use in another sampler"* and use *"in or in relation to any competitive products."* https://www.pianobook.co.uk/terms-conditions/ |
| **Leisureland / Taijiguy Mellotron** (and the Mellotron-SFZ mappings, Plogue Sforzatron) | Classic M400 tapes | The owner says the samples *"are free to use, but cannot be used in any commercial, for profit software."* (github.com/ExistentiaVirae/Mellotron-SFZ). The "old enough to be public domain" argument is legally unsound: pre-1972 US sound recordings are federally protected until 2067. "Mellotron" is also a trademark. |
| **Drolez Wavestate Pads / Minifreak Pads** | Lush hardware-synth pads (the author's own patches) | sfzinstruments lists them as CC0, but the author's own page only says *"100% royalty free for use in your sound design projects or music productions."* That's a usage grant, and the two sources conflict. Needs a written CC0 confirmation from the author. |
| **FreePats General MIDI Set** | 45 melodic + 43 perc | GPL-3 "with a special exception". GPL sample data inside a closed plugin is a risk we don't need. |
| **SamsSonor drums, No Budget Orchestra** | Drums / orchestra | CC-BY-SA. Share-alike covers our edits. Avoid. |
| **Musical Artifacts (site)** | Aggregator | Licences are **declared by the uploader and not verified** (e.g. it hosts Philharmonia samples, which may not be redistributed). Always trace back to the original source. |
| **SFZ Instruments with no licence file** | Terkelsen Marimba/Mandolin, SMDrums, TicTokMen, Clavecin, OvationGuitar, OrgueEglise, etc. | No licence = all rights reserved. |

Not examined, so treat as **not cleared**: Timbres of Heaven, Arachno, SGM, Zanderjaz choir fonts, KBH choir, Florestan choir, DSK freebies.

---

## 3. COVERAGE MAP — what the cleared set gives us, and the gaps

| Category | Cleared sources | Verdict |
|---|---|---|
| Acoustic piano | Salamander (BY), VCSL Steinway B + Kawai, Osiris (CC0), Headroom (BY), Upright KW + VSCO uprights (CC0) | **Strong** |
| Electric piano | Greg Sullivan CP80 / Pianet T / Wurli EP200 (BY); FreePats FM EP (CC0) | OK. **No cleared Rhodes**: license jRhodes, or record one. |
| Strings | VSCO 2 CE sections + solos, Karoryfer/bigcat cello, Karoryfer String Cyborgs | Good for pads and shorts. No legato. |
| Brass | VSCO 2 CE horn / trumpet / trombone / tuba, Karoryfer War Tuba | OK |
| Winds | VSCO 2 CE woodwinds, VCSL recorders + ocarina, saxes (Karoryfer, VCSL, MTG) | Good |
| **Choir / vocal pads** | Only FreePats "Synth Pad Choir" (CC0) | **GAP.** No cleared real choir found. Record or commission one, or buy a redistribution licence. |
| Mallets | VCSL (marimba, vibes, xylo, glock, tubular bells, chimes), VSCO 2 CE | **Strong** |
| Guitars | Karoryfer (Emily, Shiny, Black-and-Green), FreePats nylon + 3 clean electrics | Good |
| Basses | Karoryfer (about 10 electric/upright libraries), D. Smolken | **Excellent** |
| Drums | Karoryfer Big Rusty / Virtuosity / Swirly / Unruly (CC0); DRSKit, Muldjord, NakedDrums (BY) | **Excellent** |
| **Mellotron-style tape** | None | **GAP.** Build our own: run cleared VSCO flutes/strings and a future choir through tape DSP (wow/flutter, saturation, band-limit, the ~8 s tape-length cutoff). Name it "Tape Strings/Flute/Choir", never "Mellotron". |
| Synths | VCSL TX81Z, Karoryfer Caveman Cosmonaut / Cowsynth, FreePats pads + organs | Enough. Terrain is a synth, so its own engines cover this lane. |

**Rough factory-library size** (FLAC, preferring the larger versions): VSCO 2 CE about 2.3 GB, VCSL selected keys and mallets about 1 GB, Salamander 0.7 GB, Karoryfer picks about 1.5–2 GB, EPs + Headroom + extras about 0.5 GB. **Total ≈ 6 GB**, and trimming mic positions and duplicate layers gets it to about 3–4 GB.

---

## 4. What the multi-sampler engine needs

Terrain already has `SamplerVoice.h`, `SampleBuffer.h`, `SampleLoader.h` and `Slice.h` (see `sample-engine-STATUS-and-handoff.md`). A SoundFont instrument is those same buffers **plus a zone map**, so the multi-sampler becomes a new *front-end* over the existing voice rather than a new sampler. Its output runs through the same per-osc filter → FLOW → FX chain.

### 4.1 Internal model (one format, many importers)
Import SF2, SF3 and SFZ into **one internal zone model**; don't run two engines.
- **Instrument** → list of **Regions/Zones**, each with: sample ref, `lokey/hikey`, `lovel/hivel`, root key (`pitch_keycenter` / SF2 `overridingRootKey` or the sample header's pitch), tune (coarse/fine, cents), gain/attenuation, pan, sample `offset`/`end`, **loop mode + loop start/end**, round-robin group + position, random range, trigger type (attack/release/first/legato), exclusive/choke group (`group`/`off_by`, SF2 `exclusiveClass`), key and velocity crossfades (`xfin_*`/`xfout_*`), keyswitch, amp/filter/pitch envelopes, velocity→amp curve, amp keytracking.
- **Voice** = one zone playing through Terrain's existing oscillator voice. A note can start **several zones at once** (layers, stereo pairs, mic positions).

### 4.2 SF2 / SF3 parsing essentials
- RIFF `sfbk`: `INFO`, `sdta` (`smpl` 16-bit, optional `sm24`), `pdta` "hydra" = `phdr/pbag/pmod/pgen/inst/ibag/imod/igen/shdr`.
- Two-level resolution: **preset zone → instrument zone**. Instrument generators are absolute; preset generators are *added* offsets. Global zones supply defaults. Key and velocity ranges intersect.
- Generators we must honour: keyRange, velRange, sampleModes (0 none, 1 loop continuous, 3 loop until release), start/end/loop address offsets (+ coarse ×32768), overridingRootKey, coarse/fineTune, scaleTuning, initialAttenuation (cB), pan, **volEnv DAHDSR in timecents** (sustain in cB), modEnv → pitch/filter, modLfo/vibLfo, initialFilterFc (cents) / Q (cB), exclusiveClass, keynum/velocity overrides, reverb/chorus sends (can be ignored).
- Default modulators (vel→attenuation, vel→filter, CC7/CC10/CC1, pitch wheel). Implement the defaults first and custom `pmod/imod` later.
- Stereo: `shdr.sampleLink` + sampleType left/right pairs.
- **SF3** = the same hydra, but each sample is an Ogg Vorbis stream (sample start/end are byte offsets into the compressed chunk; loops are in decoded frames). Decode on load. JUCE's `OggVorbisAudioFormat` or `stb_vorbis` can both do it.

### 4.3 SFZ parsing essentials
- Plain-text opcodes under the headers `<control> <global> <master> <group> <region>` (+ `<curve>`, `<effect>`, `<midi>`). Inheritance runs control → global → master → group → region. Support `#define $VAR` and `#include` (factory banks rely on them heavily, e.g. Karoryfer), `default_path`, `note_offset/octave_offset`, and note names (`c#4`).
- Must-have opcodes for the libraries above: `sample, lokey/hikey/key, lovel/hivel, pitch_keycenter, tune, transpose, volume, pan, amp_velcurve_N, amp_veltrack, ampeg_* (attack/hold/decay/sustain/release/vel2*), fileg_*/cutoff/resonance/fil_type, pitcheg_*, offset, end, loop_mode (no_loop|one_shot|loop_continuous|loop_sustain), loop_start/loop_end, seq_length/seq_position (round robin), lorand/hirand (random RR), trigger=release|attack|first|legato, rt_decay, group/off_by/off_mode, xfin/xfout_lokey/hikey/lovel/hivel, sw_lokey/sw_hikey/sw_last (keyswitches), locc/hicc + on_locc (pedal/CC triggers), polyphony / note_polyphony, pitch_keytrack`.
- Many sfzinstruments banks use **SFZ v2 + ARIA extensions** (`<master>`, `$vars`, `label_ccN`, `set_ccN`, curves). Headroom, Salamander's ARIA build, Greg Sullivan and Splendid all say "ARIA-based player recommended". Test our parser against those files.

### 4.4 Playback essentials (mostly already in SamplerVoice)
- **Memory:** for a few GB of factory content, preload each sample's first ~64–256 KB into RAM and **stream the rest from disk** on a background thread (Kontakt "DFD" style). Alternatively, load a patch's samples fully into RAM on selection with a size budget. Decode FLAC/Vorbis to float or int16 in memory, and never on the audio thread.
- **Resampling:** pitch = 2^((note−root)/12 + tune/1200) × (fileSR/hostSR). At least cubic (Hermite) interpolation; a polyphase or windowed-sinc option for "HQ".
- **Loops:** sample-accurate loop points, an optional crossfade (our X-Fade knob already exists), and loop-until-release vs continuous behaviour.
- **Round robin** state per region group (and per key where needed). Random RR. Release triggers (with `rt_decay`). Choke groups (hi-hat open/closed). Voice stealing that fades the stolen voice (no clicks, per the house law).
- **Velocity and key crossfades** with equal-power curves. **Envelopes** per zone (SF2 timecents → seconds: `2^(tc/1200)`).
- Terrain's own Scan / Stretch / Formant / Spray / Start / Loop controls then act as *offsets* on top of each zone's native values.

### 4.5 Open-source parsers / engines — licences verified

| Library | Licence (verified) | Formats | Fit for Terrain |
|---|---|---|---|
| **sfizz** (sfztools) | **BSD-2-Clause** (LICENSE: "Copyright (c) 2021-2023, sfizz contributors"). Deps: **dr_libs MIT-0** (default audio I/O), Abseil Apache-2.0, KISS FFT BSD-3, pugixml MIT, Surge tuning MIT. **Avoid** its optional libsndfile backend (LGPL-2.1). | SFZ v1/v2 + many ARIA opcodes | Most complete SFZ implementation. ⚠️ **The repo was archived (read-only) on 2026-06-21**, as was sfizz-ui (the plugin repo split off in 2023-05), so vendor it and own it. Best used as the **reference parser/opcode set** or for its parser layer, feeding our own voice, rather than its whole synth (we want Terrain's filters and mod matrix). https://github.com/sfztools/sfizz |
| **TinySoundFont** (`tsf.h`) | **MIT** (2017–2025 Bernhard Schelling, based on SFZero) | SF2, plus **SF3** when `stb_vorbis` is included | Single header. Clean hydra parser: preset/instrument zone merge, loop modes NONE/CONTINUOUS/SUSTAIN, volume/mod envelopes, biquad LPF. **Does not support:** modulators, chorus/reverb sends. Good base for our SF2 → zone-model importer. Note: the repo has a *"No LLM / No AI"* policy for contributions. That limits contributing back, not use under MIT, but don't submit AI-written PRs. https://github.com/schellingb/TinySoundFont |
| **sfzq / SFZero** (Steve Folta) | **MIT** (both) | SFZ + SF2 | Smaller and simpler. The author recommends sfzq's `SFZPlayer` directory over the older JUCE-based SFZero. A good readable reference. |
| **stb_vorbis** | Public domain / MIT (dual) | Ogg Vorbis | For SF3 decoding. JUCE's built-in OggVorbis reader also works. |
| **dr_libs** (dr_flac/dr_wav) | MIT-0 / public domain | FLAC, WAV | Fast decoders. JUCE's built-in FLAC reader also works. |
| **FluidSynth** | **LGPL-2.1** | SF2/SF3 | Reference-grade SF2 implementation, but LGPL is awkward to link statically into a closed plugin. **Read it as a behaviour reference; don't link it.** |
| Polyphone | GPL-3 | SF2/SF3/SFZ editor | Useful tool for converting and inspecting banks during library prep. **Never link it.** |

**Recommendation:** build a Terrain-native `ZoneMap` plus two importers. For **SF2/SF3**, port or adapt TinySoundFont's hydra loader (MIT). For **SFZ**, use sfizz's parser/opcode tables (BSD-2, vendored) or sfzq's parser for a lighter start. Playback stays in the existing `SamplerVoice`/`SampleBuffer`. Put third-party notices (BSD-2, MIT, MIT-0, Apache-2.0 NOTICE for Abseil if used) in the About → Licences page.

---

## 5. Shipping checklist for the factory library
1. Per-file provenance CSV: source URL, licence, author, date fetched, SHA-256, and our edits.
2. Save a dated copy of every licence page or file in `Resources/Licenses/`.
3. **CC-BY content ships as plain FLAC + SFZ/zone files**, never encrypted. Credits go in the About screen and the manual. CC0/MIT content may be packed or encrypted.
4. Name nothing after trademarks ("Mellotron", "Rhodes", "Wurlitzer", "Steinway") in *product* marketing without care. Descriptive use in credits is fine; preset names like "Tine EP" and "Tape Flute" are safer.
5. Before relying on them, send permission emails to: **UIowa EMS** (would clear MIS), **Jeff Learman** (commercial jRhodes licence), **Ludovic Drolez** (confirm CC0 on the pads), **S. Christian Collins** (GeneralUser GS in a paid synth).
6. **Choir** and **tape** are the two real gaps. Budget for recording or commissioning them, or for buying a redistribution licence.

### Sources (all fetched 2026-09-23)
- VCSL — https://github.com/sgossner/VCSL
- VSCO 2 CE — https://github.com/sgossner/VSCO-2-CE
- Karoryfer free samples — https://shop.karoryfer.com/pages/free-samples · KVR announcement: https://www.kvraudio.com/forum/viewtopic.php?t=588927
- SFZ Instruments org — https://github.com/orgs/sfzinstruments/repositories · https://sfzinstruments.github.io/
- Salamander — https://github.com/sfzinstruments/SalamanderGrandPiano
- FreePats — https://freepats.zenvoid.org/ (per-instrument pages)
- Headroom — https://github.com/sfzinstruments/BengtNilsson.HeadroomPiano
- Greg Sullivan E-Pianos — https://github.com/sfzinstruments/GregSullivan.E-Pianos
- jRhodes licence — https://raw.githubusercontent.com/sfzinstruments/jlearman.jRhodes3d/master/LICENSE
- Maestro readme — https://github.com/sfzinstruments/MatsHelgesson.MaestroConcertGrandPiano
- Splendid — https://github.com/sfzinstruments/SplendidGrandPiano
- Discord GM Bank — https://github.com/sfzinstruments/Discord-SFZ-GM-Bank
- MuseScore/FluidR3 MIT — https://github.com/musescore/MuseScore/blob/master/share/sound/FluidR3Mono_License.md
- GeneralUser GS — https://github.com/mrbumpy409/GeneralUser-GS/blob/main/documentation/LICENSE.txt
- Iowa MIS — https://theremin.music.uiowa.edu/MIS.html
- Philharmonia — https://philharmonia.co.uk/resources/sound-samples/
- Sampling Plus 1.0 — https://creativecommons.org/licenses/sampling+/1.0/
- Virtual Playing Orchestra — https://virtualplaying.com/virtual-playing-orchestra/
- Pianobook EULA — https://www.pianobook.co.uk/terms-conditions/
- Mellotron-SFZ — https://github.com/ExistentiaVirae/Mellotron-SFZ
- Drolez pads — https://drolez.com/blog/music/korg-wavestate-presets-samples.php · https://drolez.com/blog/music/arturia-minifreak-presets-samples.php
- Freesound FAQ — https://freesound.org/help/faq/
- sfizz — https://github.com/sfztools/sfizz (LICENSE on `develop`)
- TinySoundFont — https://github.com/schellingb/TinySoundFont (`tsf.h` header)
- SFZero / sfzq — https://github.com/stevefolta/SFZero · https://github.com/stevefolta/sfzq
- FluidSynth — https://github.com/FluidSynth/fluidsynth
