# Organics Engine: Factory Sample Library Manifest

**Product:** Terrain (Waves Crate). This library feeds the Organics SF2/SFZ multi-sampler engine.
**Owner:** Max. **Compiled:** 2026-09-24. Every licence was checked on that date.
**Clearance pass (2026-09-24, "no emails" rule):** nothing in the factory library may depend on a permission request. The MTG saxes were cleared sound by sound (R2 closed); the Greg Sullivan EPs were removed (R4: no licence from the author); Karoryfer Weresax (CC0 alto) was added. See §8 for the cleared-status table.
**tp108 (2026-09-25):** per-key velocity-127 peak trim and the short-take tuning pass, §10.
**Round 2 (tp105, 2026-09-24):** 74 instruments. EPs are back as Waves Crate physical models (no cleared real EP exists; §9.1), 10 new cleared instruments (§9.2), a shared mechanical-noise library mapped onto 54 instruments (§9.3), measured tuning corrections `tfix` (§9.4), K-weighted loudness normalisation (§9.5) and a round-robin/audibility audit (§9.6). §8 is the cleared-status table.
**Local root:** `/Users/macshooter/Developer/VST-Plugins/organics-library/`. It lives outside the git repo. Paths below are relative to `raw/`.
**Downloaded:** **9.6 GB** (9,880 MiB on disk) from 6 sources: VCSL, VSCO 2 CE, sfzinstruments/Karoryfer, FreePats, Versilian Etherealwinds and OLPC/Berklee.
- Every GitHub file was checked against its git blob SHA-1, with 0 failures.
- Every archive passed `unzip -t` or `bsdtar`. Their SHA-256 hashes are in `ARCHIVE(S)-SHA256.txt` next to each set.
- A `LICENSE-SOURCE.txt` sits beside each set: 45 in total, each with the URL, date, exact quote and verbatim licence file.

**Tools** (`tools/`):
- `ghfetch.py`: a resume-safe, SHA-verified GitHub tree downloader.
- `analyze.py`: SFZ mapping stats plus audio QC.
- `write_licenses.py`: regenerates the `LICENSE-SOURCE.txt` files.
- `freepats_urls.txt`: the list of FreePats download URLs.

Raw analysis output is in `analysis/*.jsonl`.

> Not legal advice. The rule applied is the one in `.ideas/soundfont-research.md`: **the licence must allow redistributing the sample files themselves inside a product we sell.**
> - **Tier A** = CC0, public domain, Unlicense, or an explicit "commercial software" grant. We may convert, trim, loop, encrypt and pack these files.
> - **Tier B** = CC-BY 3.0/4.0 or MIT. These need a credit in the About screen and manual, and **must ship as plain unencrypted files with no DRM**; the EULA must not restrict them. We may still edit them (loop, trim, normalise), which counts as an "adaptation" we're allowed to share. The CC-BY 4.0 licence is the one that also asks us to say we modified them.
> - Everything else is rejected (see §5).

**How to read the ratings.** Quality is a 1–5 score based on:
- the mapping stats (velocity layers, round robins and sampling interval, counted from the SFZ files by `tools/analyze.py`),
- the format, and
- an audio check on 10 random files per patch: peak level, a tail/noise-floor estimate (the quietest 50 ms window) and clipped-file count.

Some limits on those numbers:
- The "floor" figure means something only for decaying sounds. For sustained or looped sources it just measures the quietest part of the note.
- Nobody listened to these files. The ratings are an engineering estimate, so **do a listening pass before locking the list**.
- "Step" is how far apart the sampled notes are. Step 3 means one sample every minor third, so the engine stretches each sample up to ±1–1.5 semitones.

---

## 0. Summary

- **Instrument count.** "Downloaded" counts distinct sampled instruments now on disk. "Presets" adds variants built from those samples: articulations, ensembles and tape builds.

  | Category | Downloaded | Presets |
  |---|---|---|
  | Keys | 12 | 13 |
  | Mallets & Bells | 17 | 17 |
  | Plucked | 12 | 12 |
  | Bowed Strings | 9 | 13 |
  | Woodwinds | 20 | 20 |
  | Brass | 5 | 11 |
  | Voices | 5 | 5 |
  | Organs | 7 | 9 |
  | Guitars & Basses | 7 | 9 |
  | World (beyond the cross-listed ones) | 6 | 14 |
  | Tape / "Mellotron-style" | 0 (built in-house) | 6 |
  | Tonal Percussion | 0 (cross-listed) | 6 |
  | **Total** | **≈100** | **≈135** |

- **Tier A (CC0 / Unlicense):** about 8.1 GB. **Tier B (CC-BY):** about 1.5 GB (Salamander 838 MB, Etherealwinds harp 364 MB, MTG sax 108 MB, OLPC 148 MB, Greg Sullivan EPs 21 MB, since removed from the factory library).
- **Biggest gaps:**
  1. a **real choir**
  2. ~~a Rhodes / tine EP, Wurlitzer, Pianet~~ **filled by Waves Crate physical models** (§9.1); a convincing **CP-style electric grand** is still missing (the model was not good enough), and no cleared *real* EP recording exists
  3. ~~a Clavinet~~ **filled by the Clav model** (§9.1)
  4. a **real tonewheel organ**
  5. **celesta / music box**
  6. a **steel-string acoustic guitar** and a good **nylon guitar**
  7. **sitar / koto / shamisen / oud / pan flute / shakuhachi**
  8. **brass and woodwind sections** (ensembles)
  9. **ready-made tape (Mellotron-style) instruments**. These can be built in-house from cleared sources (§1.11).

  See §4 for options and §8 for the cleared-status table.

---

## 1. Proposed factory instruments, by category

Column key:
- **Tier:** A or B, as defined above.
- **Size:** on disk, in MB.
- **Vel / RR:** velocity layers / round robins.
- **Map:** the sampled key range and the step between samples.
- **Q:** quality rating out of 5.
- **Work:** the mapping and editing work still needed.

### 1.1 Keys
| # | Patch (working name) | Source → local path | Tier | Size | Vel / RR | Map | Format | Q | Work |
|---|---|---|---|---|---|---|---|---|---|
| K1 | **Concert Grand** (Yamaha C5) | Salamander Grand Piano v3, Alexander Holm, SFZ by kinwie → `sfzinstruments/SalamanderGrandPiano` | B CC-BY 3.0 | 838 | **16** / – | A0–C8, one sample every minor third (30 notes); hammer-noise release samples, 3-layer string-resonance releases, pedal noise | FLAC 48k/24 stereo | **5** | ARIA SFZ v2 with `#include` Data/*.txt and `$vars`, so our parser must handle includes and defines. Stretch is at most ±1 st. |
| K2 | **Steinway Grand B** (pedal-up and pedal-down sets) | VCSL → `VCSL/Chordophones/Zithers/Grand Piano, Steinway B*` | A | 1361 | 3 / – | A0–C8, one sample per whole tone (42 notes). Separate Sus/NoSus recordings, release samples. | WAV 44.1k/24 stereo | 3.5 | Only 3 layers, so add velocity→lowpass to hide the steps. Room/hiss floor is about −66 dBFS; consider light denoising. Stretch ±1 st. |
| K3 | **Upright Piano** | VCSL "Upright Piano, Yamaha" → `VCSL/Chordophones/Zithers/Upright Piano, Yamaha*` | A | 260 | 5 / 2 | A0–E7, one sample every 4–5 st (13 notes), release samples | WAV 44.1k/24 | 3 | The sparse sampling needs ±2.5 st stretch, which sounds "lo-fi". Good for a Felt/Lo-fi Upright preset. |
| K4 | **Honky-Tonk / Old Player Piano** | FreePats "Old Piano FB" (Francis Bacon player piano, Piotr Barcz) → `FreePats/PianoFB-SFZ+FLAC-20200401` | A | 36 | 1 / – | chromatic, 78 notes | FLAC 44.1k/16 stereo | 3 | One layer only; use velocity→filter. Strong character. |
| ~~K5~~ | ~~Electric Grand (Yamaha CP80)~~ | Greg Sullivan E-Pianos | **REMOVED** | – | – | – | – | – | Removed 2026-09-24: the author states no licence at the source (R4). id `gsullivan.ep.cp80` = 9 stays reserved. |
| ~~K6~~ | ~~Reed EP (Wurlitzer EP200)~~ | Greg Sullivan | **REMOVED** | – | – | – | – | – | Removed (R4). id `gsullivan.ep.wurlitzer-ep200` = 3 stays reserved. |
| ~~K7~~ | ~~Pianet (Hohner Pianet T)~~ | Greg Sullivan | **REMOVED** | – | – | – | – | – | Removed (R4). id `gsullivan.ep.pianet-t` = 13 stays reserved. |
| K5b–K7b | **Tine EP, Reed EP, Pad Reed EP, Clav** (round 2) | Waves Crate physical models → `raw/TerrainModels/*` (`Tools/organics/epmodel`) | Owned | 47–61 | 5–6 / 1 | every 3 st; damper/key-up release samples; modelled key noises | FLAC 48k | model | See §9.1. Ids `terrain.ep.rhodes / .wurli / .pianet / .clav` (new numbers; 3, 9, 13 stay reserved). |
| K8 | **FM Tine EP** (DX7-style "E.Piano 1") | FreePats FM Piano #1, rendered from the Hexter emulator → `FreePats/FM-Piano1-*` | A | 25 | 3 / – | one sample every 6 st | FLAC 44.1k/24 mono | 2.5 | Needs a denser mapping (±3 st stretch is audible). It imitates Yamaha's factory patch; see §6 risk note R5. |
| K9 | **FM Bright EP** | FreePats FM Piano #2 | A | 9 | 1 / – | one sample every 6 st | FLAC mono | 2 | Filler. |
| K10 | **FM Piano / Clavisynth** (TX81Z) | VCSL TX81Z: *"patches are user-created ... sampled from the original FM hardware"* → `VCSL/Electrophones/TX81Z` | A | 132 | 3 / – | C1–C#8, one sample every 4 st | WAV 44.1k/24 mono | 3 | Clavisynth is the closest cleared thing to a Clavinet. |
| K11 | **Harpsichord** (Flemish; 8', 4', full) | VCSL → `VCSL/Chordophones/Zithers/Harpsichord, Flemish*` | A | 127 | 1 / – | F1–C6, one sample per whole tone; release (jack) samples | WAV 44.1k/24 stereo | 3.5 | Map the stops to a macro. |
| K12 | Toy / FM "Piano 1" | VCSL TX81Z Piano 1 | A | 35 | 3 / – | one sample every 4 st | WAV mono | 2.5 | Optional. |
| K13 | **Grand #3 (quiet/felt-style)**, *not downloaded* | Osiris Piano (Versilian + Karoryfer, CC0, v0.925 beta) → release zip 459 MB | A | (490) | multi / RR | full | FLAC | 4 (from docs) | Add later if disk allows. The shop describes it as *"perhaps most importantly, a quiet piano"*: a natural felt-piano preset. |

### 1.2 Mallets & Bells
| # | Patch | Source → path | Tier | Size | Vel / RR | Map | Format | Q | Work |
|---|---|---|---|---|---|---|---|---|---|
| M1 | **Vibraphone** (soft and hard mallets) | VCSL → `VCSL/Idiophones/Struck Idiophones/Vibraphone*` | A | 114 | 2–3 / – | F3–F6, one sample every 3 st | WAV 44.1k/16 stereo | 3.5 | Very clean (tail floor < −115 dBFS). Motor/tremolo can be added with our LFO. |
| M2 | **Bowed Vibes** (pad) | VCSL Vibraphone – Bowed | A | (incl.) | 1 / – | 6 notes | WAV | 3 | Needs loop points. Lovely pad source. |
| M3 | **Marimba** | VCSL Marimba | A | 38 | 1 / – | F2–C#7, 10 notes (sparse) | WAV 44.1k/24 | 2.5 | Sparse (±3 st stretch). The VSCO 2 CE marimba (not downloaded, 12 MB) can be a second layer. |
| M4 | **Xylophone** (soft, medium and hard mallets) | VCSL | A | 26 | 2–3 / – | G3–C#8, one sample every 5 st | WAV 44.1k/24 | 3 | Stretch ±2.5 st; xylophone tolerates it. |
| M5 | **Glockenspiel** | VCSL | A | 17 | 1 / – | G4–C#8, one sample every 5 st | WAV 16 | 3 | – |
| M6 | **Tubular Bells** | VCSL Tubular Bells 1 | A | 70 | 2 / – | C4–F5, one sample per whole tone | WAV 24 | 3.5 | Extend the range by stretching. |
| M7 | **Tubular Glockenspiel** | VCSL | A | 15 | 3 / – | G5–A#6 | WAV | 3 | – |
| M8 | **Hand Chimes** | VCSL | A | 49 | 1 / – | C4–C#7, one sample per whole tone | WAV 24 | 3.5 | Very clean. A good "celesta-ish" substitute. |
| M9 | **Steel Pan** | jSteelDrum by Jeff Learman (Trinidad-made pan) → `sfzinstruments/jlearman.SteelDrum` | A (Unlicense) | 43 | **5 / 3–4** | C4–B5, chromatic | FLAC 44.1k/16 stereo | **4** | Unlooped. Includes a velocity-crossfade version. |
| M10 | **Hang (D minor)** | FreePats Hang → `FreePats/Hang-D-minor-*` | A | 14 | 1 / random | 9 scale notes only | FLAC 44.1k/24 | 3.5 | Only the instrument's own notes sound, so map it to a D-minor scale preset. |
| M11 | **Water Glasses** | FreePats Glass → `FreePats/Glass-*` | A | 13 | 1 / **9** | A4–A6, chromatic (20 notes) | FLAC 48k/24 mono | 3 | – |
| M12 | **Wine Glass** (friction pad) | VCSL Wine Glasses (fast, slow) | A | 45 | 1 / – | only 4 notes | WAV 24 | 2.5 | Needs loops and wide stretch. An FX pad. |
| M13 | **Mark Tree / Bell Tree** | VCSL | A | 37 | – | FX one-shots | WAV | 3 | Map as FX keys. |
| M14 | **Nepalese Hand Bells** | VCSL | A | 2 | – | 3 hits | WAV | 3 | FX. |
| M15 | **Gong** | VCSL Gong 1 | A | 31 | **7** / – | 3 keys | WAV | 3.5 | – |
| M16 | **Balafon** (hard, soft and traditional mallets) | VCSL | A | 10 | 3 / – | C4–G6, one sample every 5 st | WAV 16 | 3 | (Could also sit under World.) |
| M17 | Music Box *(gap filler)* | OLPC Berklee44v1 `music_box_tone_1–4` → `OLPC-Berklee/Berklee44v1` | B CC-BY 3.0 | <1 | – | 4 one-shot tones, pitch unlabelled | WAV 44.1k/16 mono | 1.5 | **Not a real multisample. Music box and celesta remain gaps (§4).** |

### 1.3 Plucked (harps, zithers, lyres)
| # | Patch | Source → path | Tier | Size | Vel / RR | Map | Format | Q | Work |
|---|---|---|---|---|---|---|---|---|---|
| P1 | **Celtic Harp** | **Etherealwinds Harp II: CE**, Versilian Studios, 34-string lever harp → `Versilian-EtherealwindsHarpIICE` | B CC-BY 4.0 | 364 | 2 / 2 (random) | C2–A6, **chromatic** (58 notes) | WAV 44.1k/16 stereo | **4** | Ready to use. Also includes 7 harp FX and 16 "JordiVox" sung phrases (usable as a vocal FX key). |
| P2 | **Concert Harp** | VCSL Concert Harp → `VCSL/Chordophones/Composite Chordophones/Concert Harp*` | A | 76 | 3 / – | E1–F#7, one sample every 3 st | WAV 44.1k/16 | 3.5 | Tier A, so this is the harp to use if the preset must be encrypted. |
| P3 | Folk Harp | VCSL | A | 82 | 1 / – | C2–A6, one sample per whole tone | WAV 16 | 3 | – |
| P4 | **Dan Tranh** (Vietnamese zither): normal, tremolo, vibrato, gliss, FX | VCSL | A | 104 | up to 3 / – | B2–C6, one sample per whole tone | WAV 44.1k/24 mono | 3.5 | Articulations become keyswitches or separate presets. |
| P5 | **Bowed/Plucked Psaltery** | VCSL | A | 43 | 1 / – | G4–G6 | WAV 24 | 3 | The LongBow articulation is a pad source and needs loops. |
| P6 | **Strumstick** | VCSL | A | 89 | 3 / – | C3–C6, one sample per whole tone | WAV 24 | 3 | – |
| P7 | **Medieval Lyre** (Cithara barbarica) | Karoryfer → `sfzinstruments/cithara-barbarica` | A | 266 | 3 / **4** | 10 strings only (E–B in E major); a separate chromatic strum patch | WAV 44.1k/24 stereo | 4 | Only 10 pitches: either stretch the remaining notes or offer a scale-locked preset. |
| P8 | **Hungarian Zither** (strums and melody) | Karoryfer → `sfzinstruments/hungarian_zither` | A | 184 | 1 / 3–4 | E2–C5, chromatic (30 notes) | FLAC 44.1k/24 | 3.5 | – |
| P9 | **Ganjo** (6-string guitar-banjo) | itsclipping → `sfzinstruments/ganjo` | A | 26 | 1 / **11** | D2–C5, chromatic | WAV 44.1k/32-float | 3.5 | Convert to 24-bit. |
| P10 | Ukulele | FreePats (Flight Fireball tenor) → `FreePats/Ukulele-SFZ+WAV-20260811` | A | 4 | 1 / – | C4–C6, one sample per whole tone | WAV 48k/24 mono | 2.5 | Filler. |
| P11 | Kalimba set (Kenya, Tanzania) | VCSL → `VCSL/Idiophones/Plucked Idiophones/*` | A | 47 | 1 / 2 | chromatic-ish, B3–C6 / G2–D7 | WAV 48k/24 stereo | 3.5 | Very clean. |
| P12 | Mbira (3 types: Mavembe, Nyamaropa, Nyunga Nyunga) | VCSL | A | 81 | 1 / 2 | 14–22 notes each | WAV 48k/24 | 3.5 | Traditional tunings. Keep them untuned for character. |

### 1.4 Bowed Strings
| # | Patch | Source → path | Tier | Size | Vel / RR | Map | Format | Q | Work |
|---|---|---|---|---|---|---|---|---|---|
| S1 | **Violin Section**: sustain vibrato (normal and quiet), tremolo, spiccato, pizzicato | VSCO 2 CE → `VSCO-2-CE/Strings/Violin Section` + `ViolinEns*.sfz` | A | 102 | 2–3 / 2 (shorts) | G3–D6, one sample every 3 st | WAV 44.1k/16 stereo | 3.5 | **No loops.** Sustains need crossfade loop points (our X-Fade). Stretch ±1.5 st. |
| S2 | **Viola Section** (same articulations) | VSCO 2 CE | A | 147 | 2–3 / 2 | C3–D6 | WAV 16/24 | 3.5 | Same work as S1. |
| S3 | **Cello Section** (same articulations) | VSCO 2 CE | A | 188 | 2–4 / 2 | C2–F5 | WAV 44.1k/24 | 3.5 | Same work as S1. |
| S4 | **Solo Violin** (vibrato, quiet vibrato, tremolo, spiccato, pizzicato) | VSCO 2 CE | A | 144 | 2–3 / 2 | G3–C7 | WAV 16 | 3 | Same work as S1. |
| S5 | **Solo Contrabass** (vibrato, non-vibrato, tremolo, spiccato, pizzicato) | VSCO 2 CE | A | 159 | 2–3 / – | C1–C4, one sample per whole tone | WAV 16 | 3 | Same work as S1. |
| S6 | **Solo Cello** (sustain p/mp/mf/f, staccato, marcato, pizzicato) | Karoryfer x bigcat cello, played by Kamila Borowiak → `sfzinstruments/karoryfer-bigcat.cello` | A | 140 | **4–6 / 4** | C1–C5, one sample every 3 st | WAV 44.1k/24 mono | **4** | Deep round robins. Pizzicato is a separate patch. |
| S7 | **Double Bass** (arco 3 or 5 layers, pizzicato) | Karoryfer **Meatbass** (1958 Otto Rubner bass) → `sfzinstruments/karoryfer.meatbass` | A | 286 | **5 / 2–4** | C0–G5, one sample every 3 st | WAV 44.1k/24 mono | **4** | "Arco 3vel" is already looped. |
| S8 | **Erhu** (long, short, sul tasto, marcato) | Karoryfer "aliexpress erhu" → `sfzinstruments/aliexpress-erhu` | A | 91 | 1 / 2–4 | D3–A4, chromatic | WAV 44.1k/24 stereo | 3.5 | Two octaves only. Could also go under World. |
| S9 | **String Cyborg pads** (Zinc, Blackheart, Ironface) | Karoryfer String Cyborgs → `sfzinstruments/karoryfer.string-cyborgs` | A | 74 | 1 / – | A0–C6, **looped** | WAV 44.1k/16 mono | 3.5 | Already-looped bowed tones: ideal Terrain oscillator sources. |
| S10–13 | Ensemble "Full Strings" (layered S1+S2+S3, plus Contrabass) | built from VSCO | A | – | – | – | – | 3.5 | A preset-level layer, not new samples. |

### 1.5 Woodwinds
| # | Patch | Source → path | Tier | Size | Vel / RR | Map | Format | Q | Work |
|---|---|---|---|---|---|---|---|---|---|
| W1 | **Flute**: sustain with vibrato, non-vibrato, expressive vibrato, staccato | VSCO 2 CE → `VSCO-2-CE/Woodwinds/Flute` + `Flute*.sfz` | A | 128 | 3 (sustain), up to 9 (staccato) / random (staccato) | C4–C7, one sample every 3 st | WAV 44.1k/24 | 3.5 | Sustains need loops. |
| W2 | Piccolo | VSCO | A | 13 | 1 / – | only 5 notes | WAV 24 | 2 | Sparse: ±4 st stretch. |
| W3 | **Oboe** (non-vibrato, vibrato, staccato) | VSCO | A | 81 | 2 / – | A#3–F6, one sample every 3 st | WAV 16 | 3 | Needs loops. |
| W4 | **Clarinet** (sustain, staccato) | VSCO | A | 69 | 3–5 / random | D3–F#6 | WAV 16 | 3 | Needs loops. |
| W5 | **Bassoon** (sustain, vibrato, staccato) | VSCO | A | 70 | 3 / random | A#1–C5 | WAV 16 | 3 | Needs loops. |
| W6 | **Soprano Sax** | MTG Solo Sax (Freesound MTG packs, SFZ by kinwie) → `sfzinstruments/MTG.SoloSax` | B CC-BY 3.0 (every Freesound source sound, by MTG; kinwie's SFZ edit CC-BY 4.0). **Cleared per sound, 2026-09-24** | 108 (all 4) | 2–3 / 3 | chromatic, 33 notes | FLAC 48k/24 mono | **4** | Shipped: note samples only. Breath/key-noise files are excluded (not traceable to a Freesound sound). |
| W7 | **Alto Sax** | MTG Solo Sax | B | (incl.) | 2–3 / 3+ | G#2–A5 | FLAC 48k/24 | 4 | – |
| W8 | **Tenor Sax** | MTG Solo Sax | B | (incl.) | 2–3 / 3+ | chromatic | FLAC 48k/24 | 4 | – |
| W9 | **Baritone Sax** | MTG Solo Sax | B | (incl.) | 3–4 / 9 random | G#1–A4 | FLAC 48k/24 | 4 | – |
| W10 | **Vintage Bari Sax** (1926 Conn): sustain, marcato, staccato, subtone, growl | Karoryfer **Bear Sax** → `sfzinstruments/karoryfer.bear-sax` | A | 149 | **4–5 / 4** | C2–G#4, chromatic | WAV 44.1k/16–32 mono | **4.5** | Tier A counterpart to W9. |
| W11 | **Alto Sax** (Weresax, 2 mic positions; also "saxcordion") | Karoryfer Weresax → `sfzinstruments/karoryfer.weresax` | A | 196 | 2 / 2 | C#3–G#5, chromatic | WAV 44.1k/24 mono | 3.5 | **Shipped 2026-09-24** as `karoryfer.sax.weresax-alto` (condenser mic, 2 layers, RR reduced to 1 by the 48 MB budget). The readme says alto and the samples folder is `alto/`; the shop's "tenor" is wrong. |
| W12 | **Tenor Sax** (vibrato, non-vibrato, staccato) | VCSL Tenor Saxophone | A | 139 | 2–3 / – | G#2–F6, one sample per whole tone | WAV 48k/24 stereo | 3.5 | Needs loops. Tier A tenor. |
| W13 | Saxello | VCSL | A | 47 | 2–3 / – | one sample every 4 st | WAV 48k/24 | 3 | – |
| W14–17 | **Recorders**: baroque soprano, alto, tenor, bass (sustain, vibrato sustain, staccato) | VCSL → `VCSL/Aerophones/Edge-blown Aerophones/Baroque*` | A | 151 | 1 / – | one sample per whole tone | WAV 48k/16 stereo | 3 | Needs loops. Good "breathy flute" and tape-flute source. |
| W18 | **Ocarina** (small and typical) | VCSL | A | 88 | 1 / – | chromatic to whole tone; "Typical" has release samples | WAV 44.1k/24 | 3 | The small ocarina is noisy (floor about −44 dBFS). |
| W19 | **Harmonica**: Hohner Special 20 in C and F, Super 64 (normal, vibrato, hand vibrato, soft, accented, staccato) | VCSL → `VCSL/Aerophones/Free Aerophones/*` | A | 83 | 1 / up to 2 | one sample every 3 st; release samples | WAV 44.1k/24 mono | 3.5 | – |
| W20 | Wooden Recorder / Clarinet (FreePats) | FreePats Recorder (Eugene Vlaskin) and Clarinet (Tyler and Kaili Dence) | A | 18 | 1 / random | looped | FLAC/WAV | 2 | Filler only. |

### 1.6 Brass
| # | Patch | Source → path | Tier | Size | Vel / RR | Map | Format | Q | Work |
|---|---|---|---|---|---|---|---|---|---|
| B1 | **Trumpet**: sustain, sustain vibrato, staccato, straight mute, harmon mute | VSCO 2 CE → `VSCO-2-CE/Brass/Trumpet` | A | 129 | 2–3 / random (staccato) | E3–C6, one sample every 3 st | WAV 44.1k/16 | 3.5 | Sustains need loops. Harmon mute is a nice "Miles" preset. |
| B2 | **French Horn**: sustain (8 layers), staccato, muted | VSCO | A | 84 | **up to 8** / random | A1–F5 | WAV 16 | 3.5 | Staccato peaks at 0 dBFS and one file clips, so trim gain. |
| B3 | **Tenor Trombone** (sustain, vibrato, staccato; "OldTrombone" set) | VSCO | A | 397 | 3–7 / random | A#1–F4 | WAV 16 | 3.5 | OldTrombone carries most of the sustain/vibrato. |
| B4 | **Tuba** (sustain, staccato, keyswitch) | VSCO | A | 40 | 2–6 / **4** | F1–D4 | WAV 16 | 3 | – |
| B5 | **Folk Tuba** (staccatissimo, staccato, sustain, legato; duo and trio unison) | Karoryfer **War Tuba** (Jakub Lewicki) → `sfzinstruments/karoryfer.war-tuba` | A | 137 | **5–8 / up to 10** | D#1–C4, chromatic | WAV 44.1k/16 mono | **4** | Deepest round-robin brass in the set. |
| B6–B11 | Brass section and ensemble presets: layered Trumpet + Horn + Trombone; "Soft Brass Pad" (horn sustain looped); "Muted Brass" | built from B1–B5 | A | – | – | – | – | 3 | Preset-level work. **No real brass-section recording is cleared (gap).** |

### 1.7 Voices
| # | Patch | Source → path | Tier | Size | Vel / RR | Map | Format | Q | Work |
|---|---|---|---|---|---|---|---|---|---|
| V1 | **Solo Male "Aah"**: sustain, shorts, true legato | Karoryfer legato vocal tutorial (a CC0 subset of the paid Hadzi-Fia library, Ewe singer) → `sfzinstruments/legato_vocal_tutorial` | A | 173 | 1 / **4** (shorts) | C3–A#4, chromatic (23 notes); 396 legato transitions | WAV 44.1k/24 stereo | 3.5 | A real human voice, CC0. Stack it with our unison/chorus into a "Men's Aah" choir pad. Sustains need loops. |
| V2 | **Female / Male "Aa" and "Oo"**, solo and small ensemble (`voices_aa2` / `oo2`) | OLPC / Berklee44v5 → `OLPC-Berklee/Berklee44v5` | B CC-BY 3.0 | 52 | 1 / – | 5–8 notes per vowel | WAV 44.1k/16 mono, about 3 s | 2.5 | Sparse and short. Loop, stretch ±2–3 st, and double with unison. **Seed material, not a finished choir.** |
| V3 | Vocal Phrases (Jordi, sung) | Etherealwinds Harp II CE extras | B CC-BY 4.0 | (incl.) | – / up to 19 | 4 root keys | WAV | 3 | Phrase FX key. |
| V4 | Synth Choir Pad | FreePats Synth Pad Choir (ZynAddSubFX) | A | 7 | 1 / – | one sample every 6 st, looped | FLAC 16 | 2 | Filler / legacy GM feel. |
| V5 | Vocal FX (formants, hums, whistles, vocoder) | OLPC Berklee44v5 | B | (incl.) | – | one-shots | WAV 16 mono | 2 | Optional FX. |
| — | **Real choir (SATB aahs/oohs)** | *none cleared* | – | – | – | – | – | – | **GAP. See §4.** |

### 1.8 Organs
| # | Patch | Source → path | Tier | Size | Vel / RR | Map | Format | Q | Work |
|---|---|---|---|---|---|---|---|---|---|
| O1 | **Pipe Organ** (quiet and loud manuals, quiet and loud pedal) | VCSL Pipe Organ (the same recordings as the VSCO 2 CE organ) → `VCSL/Aerophones/Edge-blown Aerophones/Pipe Organ*` | A | 140 | 1 / – | C2–C#7, one sample every 3 st | WAV 44.1k/16 | 3 | Needs loops (the samples are long). |
| O2 | **Renaissance Organ** (8', 4', 4'+8', full) | VCSL | A | 151 | 1 / – | C2–F6, one sample per whole tone | WAV 44.1k/24 stereo | 3.5 | Needs loops. Lovely chamber-organ tone. |
| O3 | Drawbar Organ (tonewheel emulation) | FreePats, rendered from setBfree → `FreePats/DrawbarOrganEmulation-*` | A | 7 | 1 / – | one sample every 4 st, looped | WAV 44.1k/16 mono | 2.5 | Rendered from a free Hammond emulator. |
| O4 | Percussive Organ | FreePats (setBfree) | A | 14 | 1 / – | looped | WAV 16 mono | 2.5 | – |
| O5 | Rock Organ | FreePats (setBfree) | A | 14 | 1 / – | looped | WAV 16 mono | 2.5 | – |
| O6 | Church Organ (Aeolus emulation) | FreePats | A | 14 | 1 / – | one sample every 6 st, looped | WAV 16 | 2 | Filler. |
| O7 | **Accordion** (Hohner button accordion) | FreePats Button Accordion HN (Jeff Stauffer 2023) → `FreePats/ButtonAccordionHN-*` | A | 6 | 1 / – | B3–G6, one sample per whole tone; looped with release samples | FLAC 44.1k/16 stereo | 3 | – |
| O8 | "Saxcordion" (a fake accordion built from sax samples) | Karoryfer Weresax | A | (incl.) | – | – | – | 3 | Character patch. |
| O9 | "Squidotron" (Mellotron-like reed patch) | Karoryfer Squidpipes | A | (incl.) | – | – | – | 3 | See also §1.11. |
| — | **Real tonewheel B3 / Leslie; harmonium** | *none cleared* | – | – | – | – | – | – | **GAP.** Terrain's own synthesis could do a drawbar organ natively. |

### 1.9 Guitars & Basses
| # | Patch | Source → path | Tier | Size | Vel / RR | Map | Format | Q | Work |
|---|---|---|---|---|---|---|---|---|---|
| G1 | **Archtop Guitar** (mic and pickup blend, vibrato, release noises) | Karoryfer **Shinyguitar** → `sfzinstruments/karoryfer.shinyguitar` | A | 443 | **4 / up to 5** | A0–G6, one sample every 3 st; release noises | WAV 44.1k/24 mono ×2 (mic and pickup) | **4.5** | The SFZ uses `$sample_dir` defined by the Sforzando bank XML. Define it as `../Samples` in our importer. |
| G2 | **Clean Electric** (direct, flatwounds; chords patch) | Karoryfer **Emilyguitar** → `sfzinstruments/karoryfer.emilyguitar` | A | 121 | **4 / 3** (release 4, noise 5) | A1–C5 | WAV 44.1k/24 mono | 4 | DI signal, so route it through our FX (amp/cab). |
| G3 | **Clean Strat-type** (FSBS Clean #1) | FreePats → `FreePats/EGuitarFSBS-clean-*` | A | 124 | 3 / random | B1–D6, one sample every 3 st | FLAC 48k/24 stereo | 3.5 | – |
| G4 | **Jazz Electric** (FSBS Clean #2) | FreePats → `FreePats/EGuitarFSBS-jazz-*` | A | 65 | 3 / random | same as G3 | FLAC 48k/24 | 3.5 | – |
| G5 | Nylon Guitar (Spanish classical) | FreePats → `FreePats/SpanishClassicalGuitar-*` | A | 6 | 1 / – | chromatic F1–E6 | FLAC 44.1k/16 mono | 2.5 | Recorded in 2008 in poor conditions and filtered. **A good nylon guitar is still a gap.** |
| G6 | **Electric Bass** (Squier Jazz: sustain, staccato, pick scrapes, release) | Karoryfer **Growlybass** → `sfzinstruments/karoryfer.growlybass` | A | 184 | **4–5 / 4–5** | A0–C6, one sample every 3 st | WAV 44.1k/24 mono | **4** | 4 of 10 QC files peak at 0 dBFS (clipped attacks). Check by ear. |
| G7 | **Upright Bass** (pizzicato) | Meatbass pizzicato (see S7) | A | – | 5 / random | – | – | 4 | Also sits under Strings. |
| G8 | Banjo-guitar | see P9 (Ganjo) | A | – | – | – | – | 3.5 | – |
| G9 | Electric Bass YR (Yamaha RBX, finger and pick) | FreePats → `FreePats/FingerBassYR-*`, `PickedBassYR-*` | A | 7 | 1 / – | chromatic, 1 octave (D1–A2) | FLAC 24 mono | **1.5** | Clipping in 6 of 10 files and noise at −45 dBFS. **Don't ship; kept only as a reference.** |
| — | Steel-string acoustic | *none cleared* | – | – | – | – | – | – | **GAP.** (FreePats FSS Steel-String is GPL, rejected.) |

### 1.10 World
| # | Patch | Source → path | Tier | Size | Vel / RR | Map | Format | Q | Work |
|---|---|---|---|---|---|---|---|---|---|
| X1 | Erhu | see S8 | A | – | – | – | – | 3.5 | – |
| X2 | Dan Tranh | see P4 | A | – | – | – | – | 3.5 | – |
| X3 | Hungarian Zither | see P8 | A | – | – | – | – | 3.5 | – |
| X4 | Medieval Lyre | see P7 | A | – | – | – | – | 4 | – |
| X5 | Kalimba, Mbira ×3 | see P11–P12 | A | – | – | – | – | 3.5 | – |
| X6 | Kalimba (FreePats, 7 RR) | FreePats (Xavimart) → `FreePats/Kalimba-*` | A | 15 | 1 / **7** | C3–C6, 8 notes | WAV 48k/24 mono | 3 | – |
| X7 | Balafon | see M16 | A | – | – | – | – | 3 | – |
| X8 | Steel Pan, Hang | see M9–M10 | A | – | – | – | – | 4 / 3.5 | – |
| X9 | **Didgeridoo** | VCSL → `VCSL/Aerophones/Lip Aerophones/Didgeridoo` | A | 30 | 1 / – | 12 keys (drones and hits) | WAV 44.1k/24 | 3 | – |
| X10 | **Bagpipes** (chanter + drone) | Karoryfer Squidpipes (squid-mantle bag) → `sfzinstruments/karoryfer.squidpipes`; FreePats Bagpipe (Gilles Sadowski, G bagpipe) → `FreePats/Bagpipe-*` | A | 53 + 16 | 1 / 4 | G2–C6, chromatic (41 notes) | WAV 16 mono / FLAC 24 | 3 | – |
| X11 | Jaw Harp | FreePats (Sakhalin recording) | A | 3 | – | – | Hydrogen-kit WAV | 2 | Low fidelity. FX only. |
| X12 | Sitar / Khaen / Anklung one-shots | OLPC Berklee44v1 → `OLPC-Berklee/Berklee44v1` | B CC-BY 3.0 | 96 | – | not chromatic (6 sitar phrases, 2 khaen, anklung scale) | WAV 16 mono | 2 | FX and one-shot keys only. **Chromatic sitar remains a gap.** |
| X13 | Ganjo | see P9 | A | – | – | – | – | 3.5 | – |
| X14 | Tabla / hand drums / world-percussion one-shots | OLPC Berklee44v1 | B | (incl.) | – | – | WAV 16 | 2 | Optional. |

### 1.11 Tape / "Mellotron-style"
**No cleared set exists.** Every Mellotron/Chamberlin set found is a rip of the original tapes, which is rejected in §5. We build our own, and the preset names never use the "Mellotron" trademark.

**Recipe:** take a cleared, **unlooped** sustain.
1. Render one note per key (or keep the native mapping).
2. Hard-cut at **~8 s** with a fast fade, like the tape-length limit.
3. Add wow/flutter of 0.1–0.3 % at about 0.5 Hz and 6 Hz.
4. Add tape saturation and head bump.
5. Band-limit to about 60 Hz–10 kHz.
6. Add slight per-key detune/level randomness.
7. Map with **no velocity** (Mellotrons have none).

| # | Tape patch | Built from (all cleared) | Tier of result |
|---|---|---|---|
| T1 | **Tape Flute** | VSCO 2 CE FluteSusNV (+ VCSL alto recorder) | A |
| T2 | **Tape Strings** (3-violin feel) | VSCO 2 CE Violin + Viola + Cello section sustain vibrato | A |
| T3 | **Tape Choir** | Karoryfer vocal "a" (A) + OLPC voices aa2/oo2 (B) | A if built only from the Karoryfer vocal, B if the OLPC voices are used |
| T4 | Tape Brass | VSCO horn/trombone sustain | A |
| T5 | Tape Cello | Karoryfer x bigcat cello sustain | A |
| T6 | **Squidotron** (ready-made) | Karoryfer Squidpipes `02-squidotron.sfz` | A |

### 1.12 Tonal Percussion
| # | Patch | Source | Tier | Notes |
|---|---|---|---|---|
| C1 | Tubular Bells, Gong | VCSL (M6, M15) | A | – |
| C2 | Steel Pan / Hang | M9 / M10 | A | – |
| C3 | Balafon / Marimba / Xylophone | M3, M4, M16 | A | – |
| C4 | Timpani *(not downloaded)* | VCSL Timpani 1 and 2 (373 MB) or VSCO 2 CE Timpani (75 MB), both CC0 | A | Download the VSCO version when needed. |
| C5 | Water Glasses / Wine Glass | M11 / M12 | A | – |
| C6 | Hand Chimes / Nepalese bells / Mark tree | M8, M13, M14 | A | – |

---

## 2. Serum 2 multisample-category coverage

Source: the local Serum 2 install lists 143 SFZ patches in 9 categories (904 MB). The folder is `/Library/Audio/Presets/Xfer Records/Serum 2 Presets/Multisamples/Factory/`; we only read the file listing and copied nothing. These files are proprietary to Xfer, so the list is a coverage target only. Serum's own sets have **no round robins and no release triggers**, and 2–3 velocity layers on pianos. On depth, most of our Karoryfer and VSCO material already beats that.

**Legend:**
- ✅ = covered by downloaded cleared material
- 🟡 = partially covered, or a cleared candidate that still needs a manual Freesound login download (§4)
- ❌ = gap

| Serum 2 category | Serum 2 instruments | Our cleared equivalent | Status |
|---|---|---|---|
| Keys | Baby Grand Piano ×3, Upright Piano, Gaff Piano | Salamander (K1), VCSL Steinway B (K2), VCSL Yamaha upright (K3), Old Piano FB (K4); Osiris (optional) | ✅ |
| Keys | Elec. Piano Suitcase (Rhodes, about 13 velocity layers) | Nothing cleared. jRhodes GM subset: the author's own licence conflicts (see §5); tim.kahn Rhodes Mk II (CC-BY 4.0) is missing 21 notes (F4–C#6 standard naming) and is login-only | ❌ |
| Keys | Elec. Piano Wurli | Nothing cleared. Greg Sullivan EP200 removed (R4); OldBassMan 200A (Freesound pack 5726, CC-BY 4.0, 13 notes a major third apart) needs a Freesound login to download | ❌ |
| Keys | Pianet | Nothing cleared. Greg Sullivan Pianet T removed (R4); tarane468 Pianet T (Freesound 26137, CC0) is 4 notes only and login-only | ❌ |
| Keys | Clav | Only VCSL TX81Z "Clavisynth" (FM) | ❌ |
| Keys | Harmonium | Freesound cabled_mess/donyaquick harmonium (CC0, chromatic C2–D5, 96 kHz), needs a login download | 🟡 |
| Keys | MT Choir / MT Flute / MT Strings | Build Tape Choir, Flute and Strings (§1.11); Squidotron | 🟡 (build) |
| Keys | Taishōgoto | none (Dan Tranh and zithers as substitutes) | ❌ |
| Mallet | Balafon | VCSL Balafon (M16) | ✅ |
| Mallet | Gamelan | none | ❌ |
| Mallet | Glockenspiel, Marimba, Xylophone | VCSL (M3–M5); VSCO marimba, xylophone and glockenspiel available | ✅ |
| Mallet | Marxophone | none; Hungarian zither / cithara are close in timbre | ❌ |
| Mallet | Singing Bowls | none. Wine glasses, hand chimes and bells are neighbours. Recording singing bowls ourselves is trivial | ❌ |
| Plucked | Kalimba | VCSL ×2, 3 mbiras, FreePats kalimba | ✅ |
| Plucked | Harp | Etherealwinds Harp II CE (P1), VCSL Concert and Folk harps | ✅ |
| Plucked | Kora ×2, Bolon, Ronroco, Charango, Cactus Bola, Swarsangam | none (OLPC has a few one-shots of other instruments) | ❌ |
| Plucked | Oud | none | ❌ |
| Plucked | Dulcimer (pick), Zither | Hungarian zither (P8), Dan Tranh (P4), psaltery (P5), strumstick (P6), cithara (P7) | ✅ |
| Plucked | Viola / Violin pizzicato | VSCO violin, viola and cello pizzicato (S1–S4) | ✅ |
| Plucked | Acoustic guitars: 12-string, nylon ×3, Martin steel, tres, palm mute, harmonics | Nylon: FreePats Spanish guitar (weak). Steel / 12-string: none. Archtop: Shinyguitar acoustic mic | 🟡 / ❌ |
| Plucked | Electric guitars: Nero, Telecaster, palm mute | Emilyguitar, Shinyguitar, FSBS Clean and Jazz | ✅ |
| Strings | Full Strings, Violins, Violas, Celli, Contrabasses (sustain, mute, pizzicato, tremolo, gliss), solo violin, solo cello | VSCO 2 CE sections and solos (sustain, quiet, tremolo, spiccato, pizzicato), Karoryfer cello, Meatbass. No mutes or glissandi | ✅ (no mutes or gliss) |
| Strings | Double Bass (short, jazz pizzicato, orchestral pizzicato) | Meatbass arco and pizzicato, VSCO contrabass | ✅ |
| Winds | Flute, Clarinet(s), Bass Clarinet, Bassoon | VSCO flute, piccolo, oboe, clarinet, bassoon; VCSL recorders. **No bass clarinet** | ✅ / ❌ bass clarinet |
| Winds | Sax Alto (vibrato), Sax Tenor | MTG Soprano, Alto, Tenor and Baritone (cleared per sound); Weresax alto (CC0, shipped); Bear Sax; VCSL tenor sax and saxello | ✅ (stronger than Serum) |
| Winds | French Horn solo and section; Trumpet (vibrato, mute) and Trumpets; Trombones (tenor, bass), Cimbasso; Tuba | VSCO horn, trumpet (straight and harmon mutes), trombones, tuba; War Tuba. **No true sections and no cimbasso** | ✅ solo / ❌ sections |
| Choir | Ah / O / Oo in Low, High and Both | Karoryfer male "a" (solo, CC0) + OLPC female and male aa/oo, solo and small ensemble (sparse). **No real SATB choir** | ❌ (seed only) |
| Synth | Solina, JX, MKS80, SID, SY77, DX, KRG PE | Out of scope: Terrain's own engines. VCSL TX81Z, Caveman Cosmonaut (CC0, not downloaded), String Cyborgs, FreePats pads | n/a |
| Bass | Pick palm, Pluck, Five-string, Jaguar tapewound | Growlybass (Jazz bass), plus Karoryfer Black And Blue (two 5-strings, 961 MB), Pastabass, Swagbass, Fashionbass (not downloaded, all CC0) | ✅ |
| Drums | Acoustic and electronic kits | Out of scope for Organics. Karoryfer Big Rusty, Swirly, Unruly and Virtuosity drums (CC0) plus DrumGizmo kits and NakedDrums (CC-BY) are available if wanted | n/a |


---

## 3. Top 10 best-sounding cleared sets (engineering estimate; confirm by listening)

1. **Salamander Grand Piano v3** (B): 16 velocity layers, 48k/24, resonance and noise layers.
2. **Karoryfer Bear Sax** (A): 1926 Conn baritone, 5 articulations, 4–5 layers × 4 RR.
3. **Karoryfer Shinyguitar** (A): archtop with mic and pickup, 4 layers × 5 RR, release noises.
4. **Karoryfer War Tuba** (A): up to 8 layers × 10 RR, legato.
5. **Karoryfer x bigcat Cello** (A): 4–6 layers × 4 RR, sustain, staccato and pizzicato.
6. **Karoryfer Meatbass** (A): double bass, arco and pizzicato, 5 layers.
7. **Etherealwinds Harp II CE** (B): chromatic, 2 layers × 2 RR.
8. **MTG Solo Saxophones** (B, cleared per sound): 4 saxes, chromatic 48k/24, 3 RR.
9. **jSteelDrum** (A): 5 layers × 3–4 RR, chromatic.
10. **VSCO 2 CE strings, woodwinds and brass** (A): the only complete cleared orchestra. Good, not great (no loops, 2–3 layers).

Honourable mentions: Karoryfer Emilyguitar and Growlybass, VCSL Vibraphone, Renaissance Organ and Harmonicas, the Karoryfer legato vocal, and the cithara barbarica.

---

## 4. Top gaps, with concrete options

"Pending login" means the Freesound set is cleared at the source, but downloading the original files needs a (free) Freesound account. Record each sound's ID, uploader and licence at download time.

| # | Gap | Best cleared option found | Next step |
|---|---|---|---|
| 1 | **Choir (SATB aahs/oohs)** | Seed material: the Karoryfer male "a" (CC0), OLPC Berklee female and male aa/oo solo and ensemble (CC-BY 3.0), and **VocalSet** (Zenodo 1442513, CC-BY 4.0: 20 singers × 5 vowels as long tones; 2–6 GB, needs slicing) | **Recommended: commission a small choir session.** 4 voices × 3 vowels (aa, oo, mm) × chromatic range × 2 dynamics, ~1 day in a studio, fully owned. Stopgap: build the "Tape Choir" from the Karoryfer vocal and OLPC voices, or slice VocalSet (Tier B). **Re-hunted 2026-09-24: still nothing cleared.** VocalSet is solo singers, one pitch per vowel; ESMUC/MULTIVOX are songs; GitHub choirs are VPO, ROM rips or neural renders. |
| 2 | **Rhodes / Wurlitzer / CP / Pianet / Clavinet (any real EP)** | **Nothing cleared.** Searched sfzinstruments (all repos), Karoryfer, VCSL, FreePats, archive.org, Zenodo, GitHub, Wikimedia, OpenGameArt and Freesound. Best near-misses: tim.kahn Rhodes Mk II (Freesound 3957, CC-BY 4.0, 21-note hole, login-only), OldBassMan Wurlitzer 200A (Freesound 5726, CC-BY 4.0, 13 notes, login-only) | Record one (a Rhodes and a Wurli hire is ~1 day), or build from a Freesound CC-BY set if a login download is ever acceptable. The runtime's `rhodes` family default now falls back to `vcsl.keys.tx81z-fm-piano`. |
| 3 | **Clavinet** | none (jlearman/stevie-clavinet: "License is unknown") | Record one. |
| 4 | **Tonewheel organ (real B3 + Leslie)** | FreePats setBfree renders (CC0); hammondman Freesound (CC0, low fidelity) | Terrain can synthesise drawbars natively (additive). Record a real B3 later. **Re-hunted 2026-09-24: still nothing cleared.** The only real cleared tonewheel audio is Chris Beckstrom's Hammond M-100 drones (archive.org, CC-BY 4.0): ~15 chords, not playable. |
| 5 | **Celesta / music box / toy piano** | Celesta: **pjcohen (Freesound 23108, CC-BY, chromatic G2–G6)** and Macsat-Rd (23781, CC0, 1 octave). Toy piano: **Framing_Noise (19485, CC0, 3 velocity layers)** and nikerk Schoenhut (42113, CC0). Music box: none | Download with a Freesound login. Music box: record one (cheap). |
| 6 | **Harmonium / reed organ** | cabled_mess/donyaquick harmonium (Freesound 29512, CC0, chromatic C2–D5, Yale recording) | Download with a Freesound login. |
| 7 | **Steel-string acoustic, 12-string, a good nylon guitar** | none cleared (FreePats steel-string is GPL) | Record, or buy a redistribution licence. **Re-hunted 2026-09-24: still nothing cleared.** Nylon: the UIowa MIS guitar is out (R8, no explicit redistribution grant). Steel: AG-PT-set (Zenodo, CC-BY 4.0) is piezo-pickup only and 6.75 GB. Karoryfer Shinyguitar's mic channel (archtop, CC0) is the closest shipped substitute. |
| 8 | **Upright and alternative pianos** | beskhu Choiseul upright (Freesound 17088, **CC0, all 88 keys**, 1 layer); Sadiquecat living-room upright (43099, CC0, 192 kHz) | Download with a Freesound login. Better than VCSL Yamaha's sparse mapping. |
| 9 | **Harpsichord, chromatic** | pjcohen harpsichord (Freesound 21464, CC0, 60 notes chromatic) | Optional, pending login. |
| 10 | **Melodica / piano accordion** | urlande Hohner melodica (Freesound 9578, CC0, chromatic, short notes); Miles_Thompson Cellini accordion (31477, CC0, **lossy MP3/M4A**) | Melodica: pending login. Accordion: FreePats HN is already downloaded. |
| 11 | **Solo violin (better than VSCO)** | ldk1609 violin packs (Freesound 3559/3560/3561/3564/3565/3578, CC0: arco vibrato and non-vibrato, pizzicato, tremolo, spiccato) | Pending login. |
| 12 | **Sitar, koto, shamisen, oud, pan flute, shakuhachi, duduk, gamelan, kora, singing bowls** | Only OLPC one-shots and phrases | Commission a "world" session, or buy from a vendor with a redistribution clause. Singing bowls are trivial to record ourselves. |
| 13 | **Brass and woodwind sections, bass clarinet, cimbasso** | Stack VSCO solos (preset-level) | Commission, or accept stacked solos. **TinySOL** (IRCAM, CC-BY 4.0 on Zenodo, 1 GB) has many chromatic ordinario notes, but IRCAM's own conflicting licence (R9) needs confirmation first. |
| 14 | **Tape / Mellotron-style** | Build from cleared sources (§1.11); Squidotron | Build in the Organics preset pipeline. Never ship tape rips. |
| 15 | **University of Iowa MIS** (whole orchestra, piano, nylon guitar, alto/soprano sax, pp–ff) | Grant: *"may be downloaded and used for any projects, without restrictions"* | **Out under the no-emails rule**: it never says the files themselves may be redistributed, and confirming that needs a letter. The nylon guitar (Raimundo 118, 3 dynamics, E2–B5 per string) would otherwise fill the nylon gap. |


---

## 5. REJECTED sources (do not re-add)

| Source | Why rejected | Evidence (checked 2026-09-24) |
|---|---|---|
| **Sonatina Symphonic Orchestra** | CC Sampling Plus 1.0 allows only non-commercial distribution of the whole work | https://creativecommons.org/licenses/sampling+/1.0/ |
| **Virtual Playing Orchestra** | Mixes Sampling+ with CC-BY-SA. The author says *"I do not feel it is right to repackage and sell this library in part or in whole for profit"* | https://virtualplaying.com/virtual-playing-orchestra/ |
| **Philharmonia Orchestra samples** | *"must not be sold or made available 'as is' (i.e. as samples or as a sampler instrument)"* | https://philharmonia.co.uk/resources/sound-samples/ |
| **Spitfire LABS** | EULA: *"expressly forbids resale or other distribution of the Products or their derivatives… samples, multi-samples… in a sampler"* | https://www.spitfireaudio.com/en-us/pages/spitfire-audio-end-user-license-agreement |
| **Orchestral Tools free** (Layers, BFO, SINEfactory) | The EULA forbids use in sample-based products, and the files are encrypted in SINE | https://www.orchestraltools.com/legal/license-agreement |
| **Decent Samples freebies** | *"You may not redistribute any of the samples or sample libraries on their own, unless you have been given written permission"* | https://www.decentsamples.com/decent-samples-end-user-license-agreement/ |
| **Pianobook** (all packs) | The EULA forbids redistribution and use *"in or in relation to any competitive products"* | https://www.pianobook.co.uk/terms-conditions/ |
| **Ivy Audio Piano in 162** (and other Ivy pianos) | Free for music; repackaging or redistributing is prohibited | Ivy Audio licence page |
| **Maestro Concert Grand** (Mats Helgesson) | *"may not sell this sound set or any of the samples"* | https://github.com/sfzinstruments/MatsHelgesson.MaestroConcertGrandPiano |
| **Splendid Grand Piano** | Its "AKAI public domain" claim has no primary source | https://github.com/sfzinstruments/SplendidGrandPiano |
| **jRhodes3c / jRhodes3d** (Jeff Learman), **including the jRhodes "005-Electric Piano 1" CC0 subset in the Discord GM bank** | Full sets are CC BY-NC. The GM subset's CC0 header was written by Jeff Learman himself (commits 05d5ed8b, 945cca9c), but his current jRhodes3c LICENSE says: *"To distribute the samples themselves, such as in an application, software instrument, or as a sample set, the jRhodes samples are licensed under CC BY-NC-SA 4.0 … To use the samples in a commercial product, please contact me"*. Two conflicting statements from the same rights holder, and settling them needs an email, so it is out | https://github.com/sfzinstruments/jlearman.jRhodes3c/blob/master/LICENSE |
| **GeneralUser GS** | The licence permits it, but the author himself warns that some sample origins are uncertain in commercial software | https://github.com/mrbumpy409/GeneralUser-GS |
| **FluidR3 / MuseScore General** | MIT, but a 2000-era compilation with unverifiable per-sample provenance. Test and GM-fallback use only; **not factory** | MuseScore FluidR3Mono_License.md |
| **Leisureland / Taijiguy Mellotron, Mellotron-SFZ, Plogue Sforzatron sets, Sonic Bloom "SB Mellotron", M-Tron / Tapeotronic / Chamberlin sets** | Tape rips. *"free to use, but cannot be used in any commercial, for profit software"*. Also the Mellotron trademark | https://github.com/ExistentiaVirae/Mellotron-SFZ |
| **Drolez Wavestate / Minifreak pads** | Two sources conflict: sfzinstruments says CC0, but the author only says "royalty free for… music productions" | drolez.com |
| **FreePats GM Set, GM Percussion, FSS Steel-String Guitar** (FlameStudios) | GPL-3 with an exception | https://freepats.zenvoid.org/Guitar/steel-acoustic-guitar.html |
| **FlameStudios Kay 5-String Banjo** (sfzinstruments) | GNU GPL (LICENSE.txt: *"This sample is released under the GNU GPL license"*) | https://github.com/sfzinstruments/FlameStudios.Kay5StringBanjo |
| **G-Town Church Sampling Project** | CC Sampling Plus 1.0 | https://github.com/sfzinstruments/GTownChurchSamplingProject |
| **SamsSonor drums, No Budget Orchestra, Westlund extras** | CC-BY-SA | sfzinstruments / VPO |
| **Starbirth Kudu Shofar** | *"you MAY NOT sell the samples, or include them in a compilation for samples for sale"* | sfzinstruments README |
| **sfzinstruments repos with no licence**: Terkelsen Marimba/Mandolin, Kastendieck SteelDrum, Krumhorn (sampled from an Ensoniq EPS **ROM**), Clavecin, OvationGuitar, OrgueEglise, PickedLapharp, SMDrums, TicTokMen, DamiensFunkyGuitar, Project16Rickenbacker4001, EthanWiner.Soundfonts (port) | No licence means all rights reserved (Krumhorn is also a ROM rip) | `gh repo list sfzinstruments` |
| **Discord SFZ GM Bank** | No repo licence. Many programs are placeholders. The sitar/shanai CC0 headers have unverifiable provenance | https://github.com/sfzinstruments/Discord-SFZ-GM-Bank |
| **Karoryfer Marie Ork** | Excluded by Karoryfer itself (*"Only Marie Ork has a different license"*) | shop.karoryfer.com/pages/free-samples |
| **Karoryfer Hadzi-Fia (full), Torgbe Choir, Three Tagelharpas** | Paid commercial products (only the CC0 tutorial subset is used) | karoryfer.com |
| **Mihai Sorohan Vowel Ensemble Choir** | *"You may not use it for commercial sample libraries"* | bigcatinstruments.blogspot.com |
| **Zanderjaz choir fonts** | *"Zanderjaz did not create any of the SoundFonts… without complete creator, source, or licensing information"* | https://www.zanderjaz.com/downloads/soundfonts/choirs/ |
| **OrchideaSOL** | The audio is under the Ircam Forum License (only the metadata is CC-BY) | https://zenodo.org/records/3740399 |
| **NSynth** | CC-BY, but built from 1,006 commercial-library instruments | https://magenta.tensorflow.org/datasets/nsynth |
| **Carlos_Vaquero Freesound packs** | CC-BY-NC 4.0 | https://freesound.org/people/Carlos_Vaquero/ |
| **Musical Artifacts** (aggregator) | Licences are declared by uploaders and often wrong (e.g. a "Yamaha MOTIF Accordion" labelled CC-BY) | musical-artifacts.com |
| **MIMO** museum recordings | Rights vary by institution; not viable | – |
| **Florestan Woodwinds, Timbres of Heaven, Arachno, SGM, KBH choir** | Unknown or composite provenance, often ROM-derived | – |
| **Meadowlark factory library** | CC0 but empty | Codeberg |
| **Versilian legacy freeware page** | No licence stated (the content is covered by VCSL anyway) | – |
| **Keppy's Steinway Piano** | CC BY-ND 4.0, plus *"Don't implement these samples in your personal soundfonts"* | github.com/rastating/Keppy-Steinway-Piano |
| **Bigcat Instruments** | Nothing original: repackaged VCSL/VSCO (use the originals) plus NSynth-derived material | bigcatinstruments.blogspot.com |
| **Dehli Musikk / benjamindehli** (MidnightWurli, reed organ, lo-fi tape piano, toy piano, music box) | GPL-3.0 | github.com/benjamindehli |
| **Pettinhouse Vinyl Hammond Free** | Only the SFZ mapping is CC-BY; the samples have no redistribution grant | pettinhouse.com |
| **Bandshed / MSLP Rhodes, Wurli, Clavinet, organs** | No licence; unknown source | bandshed.net/sounds/sfz/ |
| **GrandOrgue sets by Lars Palo** | CC BY-SA 2.5 / 4.0 | familjenpalo.se/vpo/download/ |
| **Sonus Paradisi** (including free demos), **Piotr Grabowski**, **pbrd.nl**, **Jeux d'orgues** | Resampling and redistribution forbidden, NC-SA, or proprietary | respective terms pages |
| **KeyPleezer LivingRoom Upright** | CC-BY-SA 4.0 | sfzinstruments site data |
| **Project16 Rickenbacker 4001** | CC BY-NC-SA 3.0 | sfzinstruments |
| **Korg Wavestate-based pads** (SHLD / Drolez) | Resamples of a Korg ROM, and the licence sources conflict | – |
| **"Club Sandwich"** | No such library could be found, so there was nothing to clear | – |
| **Ethan Winer SoundFonts** | *"any way you'd like, royalty free, including for commercial projects"*, but no explicit redistribution grant, recordist not named, and the SFZ port has no licence | https://ethanwiner.com/ewsf2.html (could be cleared with one email) |
| **Greg Sullivan E-Pianos** (CP80, Pianet T, Wurlitzer EP200; removed 2026-09-24) | Only the SFZ porter states CC BY 3.0 (*"with the author permission with the request for attribution"*). The author's own pages state no licence: sullivang.net (live, 2026-09-24) and the archive.org copies of 2004-10-25 (`/samples/cp80.html`, `/samples/pianet_t.html`, `/samples/wurlitzer_ep200.html`) and 2013-07-23 (`/home`, `/cp80-electric-grand`, `/hohner-pianet-t`, `/wurlitzer-ep203w`) only say *"Greg's free electric piano samples for Gigastudio/Gigasampler"*. His `/conversions` page links the SFZ repo with thanks but grants nothing | https://www.sullivang.net/ ; http://web.archive.org/web/20041025071305/http://www.sullivang.net:80/samples/cp80.html ; http://web.archive.org/web/20130723021139/http://www.sullivang.net/cp80-electric-grand |
| **Discord GM bank "003-Electric Grand Piano"** (CP80) | Its "CC0, Greg Sullivan" line was written by the bank maintainers, not by Greg Sullivan | sfzinstruments/Discord-SFZ-GM-Bank |
| **Freesound originals** (tim.kahn Rhodes, OldBassMan Wurlitzer, tarane468 Pianet, cabled_mess harmonium, pjcohen celesta …) | Licences are fine, but every original download needs a Freesound login; the only open route is lossy MP3 previews. Not used | freesound.org |
| **AG-PT-set** (Zenodo 10159492, CC-BY 4.0, steel-string guitars) | Recorded through the guitars' built-in piezo pickups; 6.75 GB (over the download budget) | https://zenodo.org/records/10159492 |
| **VocalSet** (Zenodo 1442513, CC-BY 4.0) | Solo singers, one pitch per vowel for long tones: not a choir and not a chromatic multisample | https://zenodo.org/records/1442513 |
| **Chris Beckstrom Hammond M-100 drones** (archive.org, CC-BY 4.0) | Real tonewheel, cleared, but ~15 chord/cluster drones, not a playable multisample | archive.org/details/beckstrom_hammond_organ_drones |
| **Round-2 EP hunt (2026-09-24)**: musical-artifacts "Wurlitzer (SFZ)" #645 and "Clavinet (SFZ)" #646 (uploader Lithalean) | The site's own API marks the licence `gray` (unknown); nothing traces to the recordist | musical-artifacts.com/artifacts/645, /646 |
| musical-artifacts "Simple Rhodes" #9601 ("public") | Uploader-declared, untraceable, probably synth-made | musical-artifacts.com/artifacts/9601 |
| Farfisa Hydrogen kit (Klaatu) | CC BY-SA | musical-artifacts.com/artifacts/308 |
| Bitsonic Keyzone Electric Piano | Extracted from a commercial plugin | musical-artifacts.com/artifacts/2574 |
| **NSynth** (re-checked round 2) | Magenta's own page: notes generated "for 1,006 instruments from commercial sample libraries"; the dataset's CC BY 4.0 cannot clear those libraries' rights | magenta.tensorflow.org/datasets/nsynth |
| **Headroom Piano** (Bengt Nilsson, Yamaha C3) | Only the porter (kinwie) states CC BY 4.0 "with the author's permission" — the same pattern as the Greg Sullivan EPs (R4) | github.com/sfzinstruments/BengtNilsson.HeadroomPiano |
| **Karoryfer Scarypiano** | CC0 from Karoryfer, but built on University of Iowa piano samples (R8: no explicit redistribution grant) | github.com/sfzinstruments/karoryfer.scarypiano |
| UIowa MIS guitar (re-proposed round 2) | Stays out (R8): "may be downloaded and used for any projects, without restrictions" never grants redistribution of the files | theremin.music.uiowa.edu/MIS.html |
| **NeoSoundFonts SP-GT-Classical-Guitar, SimonePiervergili-OldSteelGuitar** | CC0 LICENSE added by the uploader, not the recordist; the recordist's other uploads include resampled commercial products | github.com/NeoSoundFonts |

---

## 6. Licence-risk notes for a human to review

- **R1: Karoryfer README wording.** Older Karoryfer READMEs say *"Royalty-free for all commercial and non-commercial use"*, which on its own is a usage grant. However, every repo's LICENSE file is **CC0 1.0**, and the shop page explicitly relicensed everything: *"we changed that and now they are all CC0"*. **Low risk.** Keep the dated shop-page quote (it's in every `LICENSE-SOURCE.txt`).
- **R2: MTG Solo Sax. CLOSED 2026-09-24.** All 298 sounds in Freesound packs 20239/20247/20251/20253 were fetched one by one: every page shows "Attribution 3.0" (http://creativecommons.org/licenses/by/3.0/), uploader MTG, and *"Recorded in the context of the good-sounds.org project from the Music Technology Group, Universitat Pompeu Fabra, Barcelona"*, so the uploader is the recordist. Each of the 298 shipped note files was matched to exactly one sound by waveform correlation against the public preview (r ≥ 0.925, runner-up at least 0.1 lower). Evidence: `Tools/organics/provenance/mtg-solosax-freesound.csv` (per file) and `mtg-sax-packs-licence-audit.csv` (per sound), copied into each instrument's `source/`; per-file columns in `source/provenance.csv`. The breath and key-click files could not all be traced, so no instrument ships them (recipe `dropMatch`). Licence recorded as CC-BY-3.0 (the source); kinwie's SFZ is CC-BY 4.0; credit both.
- **R3: OLPC / Berklee.** The Internet Archive metadata says CC-BY 3.0, and the OLPC wiki lists the Berklee material as CC-BY. The recordings were made for OLPC, so the provenance is clean. **Low risk.**
- **R4: Greg Sullivan E-Pianos. RESOLVED BY REMOVAL 2026-09-24.** The author's own pages (live and archived, §5) state no licence; only the SFZ porter states CC BY 3.0. All three instruments are out of `index.json`, their compiled folders and installed links are deleted, and ids 3, 9 and 13 stay reserved in `ids.json`.
- **R5: FreePats FM Piano 1** is a render of the Hexter DX7 emulator playing (an imitation of) Yamaha's factory "E.PIANO 1" patch. A synth timbre isn't copyrightable as such, but because this is patch-data-derived, prefer VCSL TX81Z (user-made patches) for the flagship FM EP. **Low–medium risk.**
- **R6: FreePats setBfree / Aeolus / ZynAddSubFX renders.** These are CC0 renders of GPL *software*. The GPL covers the program, not audio rendered by it. **Low risk.**
- **R7: CC-BY sets must not be encrypted.** This covers Salamander, the Etherealwinds harp, MTG Sax and OLPC. If the Organics engine packs or encrypts its factory content, these files must be stored as plain FLAC/WAV + SFZ. The alternative is to replace them with Tier A equivalents: VCSL Steinway, VCSL Concert Harp, Bear Sax + VCSL Tenor, the Karoryfer vocal.
- **R8: University of Iowa MIS** (not downloaded): *"may be downloaded and used for any projects, without restrictions"*. It's permissive, but it does not explicitly allow redistribution, and confirming it needs a letter. **Out under the no-emails rule.**
- **R9: TinySOL** (IRCAM, on Zenodo as CC-BY 4.0) was **not downloaded**. The same IRCAM recordings are distributed as OrchideaSOL under the restrictive Ircam Forum License. That conflict needs IRCAM's written confirmation before use.

---

## 7. About-screen credits (all Tier B sources; required)

Paste this into the About → Credits page and the manual. The CC-BY files must ship unencrypted (R7). If any file was edited (looped, trimmed or normalised), keep the "Modified by Waves Crate" note.

```
Terrain includes sampled instruments from the following open libraries:

Salamander Grand Piano v3 by Alexander Holm (SFZ mapping by kinwie, sfzinstruments.github.io).
  Licensed under CC BY 3.0 — https://creativecommons.org/licenses/by/3.0/  — Modified by Waves Crate.
Etherealwinds Harp II: CE by Versilian Studios LLC. Licensed under CC BY 4.0 —
  https://creativecommons.org/licenses/by/4.0/ — Modified by Waves Crate.
MTG Solo Saxophones: samples recorded by the Music Technology Group, Universitat Pompeu Fabra (good-sounds.org,
  freesound.org/people/MTG), licensed under CC BY 3.0 — https://creativecommons.org/licenses/by/3.0/ ;
  SFZ edit by kinwie (sfzinstruments), CC BY 4.0 — https://creativecommons.org/licenses/by/4.0/ — Modified by Waves Crate.
Voice and world-instrument samples by Berklee College of Music, recorded for Richard Boulanger and the
  One Laptop per Child project. Licensed under CC BY 3.0 — https://creativecommons.org/licenses/by/3.0/ — Modified by Waves Crate.

With thanks (public-domain / CC0 sources, credit not required):
Versilian Studios (VSCO 2 Community Edition, Versilian Community Sample Library),
Karoryfer Samples, Jeff Learman (jSteelDrum), itsclipping (Ganjo), and the FreePats project
(Roberto, Piotr Barcz, Mateusz Dąbrowski, Jeff Stauffer, Gilles Sadowski, Eugene Vlaskin, Tyler & Kaili Dence, Xavimart, Andrea Biasior).
```

If Freesound CC-BY packs are added later, append a line for each, for example: `Celesta samples by pjcohen (freesound.org/people/pjcohen), CC BY 4.0`. Use the exact licence version shown on each sound's page at download time.


---

## 8. Cleared status (2026-09-24)

Max's rule: *"no emails, I want to be able to just put them in"*. Anything that needs a permission request is out. The authoritative list of shipped instruments is `Resources/Organics/index.json` (74 instruments after round 2); credits are in `Resources/Organics/CREDITS.md`.

| Set | In the factory library | Licence (stated by) | Status | Evidence |
|---|---|---|---|---|
| VCSL, VSCO 2 CE (Versilian) | 33 instruments | CC0 (Versilian Studios) | Cleared | `LICENSE-SOURCE.txt` per set |
| Karoryfer (Bear Sax, War Tuba, cello, Meatbass, erhu, zither, guitars, Growlybass, vocal, **Weresax alto (new)**) | 12 instruments | CC0 (Karoryfer, repo LICENSE + shop page) | Cleared (R1 low) | `LICENSE-SOURCE.txt` per set |
| FreePats | 8 instruments | CC0 (FreePats / named authors) | Cleared | FreePats pages |
| jSteelDrum (Jeff Learman) | 1 | Unlicense (author) | Cleared | repo LICENSE |
| Ganjo (itsclipping) | 1 | CC0 (author) | Cleared | repo LICENSE |
| Salamander Grand v3 (Alexander Holm) | 1 | CC BY 3.0 (author) | Cleared, credit required | `LICENSE-SOURCE.txt` |
| Etherealwinds Harp II CE (Versilian) | 1 | CC BY 4.0 (publisher) | Cleared, credit required | `LICENSE-SOURCE.txt` |
| **MTG Solo Saxophones** ×4 | 4 | CC BY 3.0 on every one of 298 Freesound sounds (uploader = recordist MTG/UPF); SFZ edit CC BY 4.0 (kinwie) | **Cleared per sound (R2 closed)**, credit required | `Tools/organics/provenance/*.csv`, `source/provenance.csv` |
| **Greg Sullivan E-Pianos** ×3 | **removed** | only the porter states CC BY 3.0 | **Out (R4)**; ids 3, 9, 13 reserved | §5 |
| jRhodes (incl. GM CC0 subset), UIowa MIS, TinySOL, Ethan Winer, Karoryfer paid choirs | not included | conflicting or not explicit; each would need an email | Out | §5, §6 |
| Freesound-only sets (Rhodes Mk II, Wurlitzer 200A, harmonium, celesta, uprights …) | not included | fine, but originals need a login | Out (login) | §4 |
| **Round 2 additions** | | | | |
| Waves Crate EP models (Tine EP, Reed EP, Pad Reed EP, Clav) | 4 | Proprietary-WavesCrate (rendered by our own `epmodel/`; no third-party audio; the DO-NOT-SHIP Greg Sullivan files were only *measured*) | Owned, cleared | `raw/TerrainModels/LICENSE-SOURCE.txt`, `epmodel/VALIDATION.md` |
| VCSL Kawai grand, Knight upright, Timpani 2, Ocarina (typical), Mbira Nyamaropa | 5 | CC0 (Versilian Studios) | Cleared | `raw/VCSL/LICENSE-SOURCE.txt` |
| Osiris Piano (Versilian + Karoryfer) | 1 | CC0 (licence file inside the release + repo licence) | Cleared | `raw/sfzinstruments/Osiris_Piano/LICENSE-SOURCE.txt` |
| Karoryfer Black And Green Guitars, Cithara Barbarica, Sneakybass, String Cyborgs (Zinc) | 4 | CC0 (Karoryfer; R1) | Cleared | `LICENSE-SOURCE.txt` per set |
| Shared noise library (§9.3) | on 54 instruments | CC0 (Karoryfer sets) · CC BY 3.0 (Salamander key-action noises: credit lines added to CREDITS.md for every piano that plays them) · Waves Crate (EP model noises) | Cleared, credit where CC-BY | per-file origin in each instrument's `source/provenance.csv` |
| Headroom Piano, Scarypiano, UIowa guitar, musical-artifacts EP/Clav uploads, NSynth | not included | porter-only / UIowa-derived / uploader-only / commercial-library-derived | Out | §5 |


---

## 9. Round 2 (tp105, 2026-09-24)

### 9.1 Electric pianos: Waves Crate physical models (owned, no permission needed)
A second hunt found **no cleared real recording** of a Rhodes, Wurlitzer, Clavinet, Pianet, CP or combo organ (GitHub incl. all 70 sfzinstruments repos, musical-artifacts, archive.org, Wikimedia, Zenodo, OpenGameArt, Codeberg, itch.io; rejections in §5). NSynth stays out. So the EPs are rendered offline by our own models in `Tools/organics/epmodel/` (numpy/scipy, deterministic, `python3 epmodel/render_all.py`) into `raw/TerrainModels/`, then compiled like any SFZ. Nothing from the DO-NOT-SHIP Greg Sullivan recordings is used as audio; they were only measured (harmonic levels, centroids, T60) to fit and check the models. Full tables: `epmodel/VALIDATION.md`. Nobody has listened yet: **do a listening pass**.

| Id (display name) | Model | Keys / zones | Layers / RR | Release / noise | Measured against |
|---|---|---|---|---|---|
| `terrain.ep.rhodes` (Tine EP) | cantilever tine (Euler-Bernoulli modes) + tonebar, neoprene hammer, asymmetric electromagnetic pickup | 28–100, every 3 st | 6 / 1 | 12 damper releases; modelled key-down/up noise | physics: bell partial 6.3–7.4× f0, −11…−22 dB in the first 50 ms, < −40 dB after 1 s; H2 −35 dB (pp) → ≈0 dB (ff) in the lower half (bark); T60 19 s (low) → 1.5 s (high) |
| `terrain.ep.wurli` (Reed EP) | steel reed, felt hammer, electrostatic pickup 1/(d0−x), preamp soft clip | 33–96, 21 zones | 6 / 1 | 11 damper releases; key noise | 42 reference notes: median error H2 5.0 dB, H3 2.6 dB, attack centroid 15 %; bark: H2 −13 → +16 dB and H3 −15 → +14 dB pp→ff at E2 |
| `terrain.ep.pianet` (Pad Reed EP) | adhesive pad plucking a reed, pickup | 29–89, 20 zones | 5 / 1 | 10 pad releases; key noise | 33 reference notes: median error H2 2.4 dB, H3 2.6 dB, attack centroid 22 % |
| `terrain.ep.clav` (Clav) | struck string on the tangent/anvil, yarn damper, two single-coil pickups (comb) | 29–88, 20 zones | 6 / 1 | 20 key-up "thwack" releases; key noise | physics: attack twice as bright at ff; +3…4 ¢ strike glide; key-up blip at 0.84–0.89 f0 as predicted; T60 5.1 → 1.8 s |
| CP-style electric grand | `cp.py` | — | — | — | **Not shipped**: misses the CP's attack clang (bass f/ff attack centroid 4–6× f0 vs 15–22× in the reference, median attack error 39 %). Honest verdict: not convincing. |

### 9.2 Ten new cleared instruments
| Id | Name | Category / family | Licence | Layers / RR | RAM MB | Notes |
|---|---|---|---|---|---|---|
| `vcsl.keys.kawai-grand` | Kawai Grand | Keys / grand | CC0 (VCSL) | 4 / 1 | 101.6 | release samples; room hiss floor ≈ −48 dB rel. peak |
| `vcsl.keys.upright-knight` | Knight Upright | Keys / upright | CC0 (VCSL) | 2 / 1 | 80.4 | release samples; pedal-CC regions dropped |
| `osiris.keys.piano` | Osiris Piano | Keys / grand | CC0 (Versilian + Karoryfer) | 2 / 1 | 108.3 | mic A only; its samples keep the pre-note key/hammer noise ("Ptah" full) |
| `vcsl.perc.timpani` | Timpani | Percussion / marimba | CC0 (VCSL Timpani 2) | 3 / 2 | 41.8 | `unpitched` for tfix |
| `karoryfer.guitar.black-and-green` | Hollowbody Guitars | Plucked / guitar | CC0 (Karoryfer) | 2 / 1 | 45.8 | 4 artics: Green/Black × Twang/Staccato |
| `karoryfer.plucked.cithara` | Medieval Lyre | Plucked / harp | CC0 (Karoryfer) | 1 / 2 | 43.4 | finger, nail, sul tasto; 10 strings stretched to 52–72 |
| `karoryfer.bass.sneaky` | Jazz Pizz Bass | Strings / bass | CC0 (Karoryfer) | 1 / 1 | 28.9 | quiet late-night pizzicato + mute |
| `karoryfer.strings.cyborg-zinc` | Bowed Bass Pad | Strings / bass | CC0 (Karoryfer String Cyborgs) | 1 / 1 | 12.4 | looped bowed double bass |
| `vcsl.winds.ocarina` | Ocarina | Winds / flute | CC0 (VCSL) | 1 / 1 | 14.3 | sustain + vibrato, releases |
| `vcsl.mallets.mbira` | Mbira | Mallets & Bells / kalimba | CC0 (VCSL) | 1 / 3 | 11.1 | traditional tuning (use Tuning = As recorded) |

Downloads: 2.1 GB (VCSL Kawai/Knight/Timpani 2 1.17 GB, Osiris 437 MB zip, Black And Green 581 MB, Sneakybass). No real choir, tonewheel organ, celesta or cleared acoustic guitar was found (research in §5).

### 9.3 Mechanical noise library (`noiselib.py` → `raw/TerrainNoise/<set>/`, mapped by `noisemaps.py`)
Every noise is **extracted from a recording we already ship** (cut, mono, filtered, faded, peak −1 dBFS; origin + cut points + sha256 in the set's `manifest.json` and in each instrument's `source/provenance.csv`). No new downloads, logins or permissions.

| Set | trig | Source | Mapped onto |
|---|---|---|---|
| `piano-keydown` (30) | on | Salamander "HammerNoise" key-action transients (CC BY 3.0) | Salamander, Steinway B, Yamaha upright, Old Upright, Kawai, Knight |
| `piano-keyup` (88, per key) | off | Salamander key-release/damper noises (CC BY 3.0) | Steinway B, Yamaha upright, Old Upright, Kawai, Knight, Osiris (Salamander keeps its own) |
| `model-*-on/off` (6+6 each) | on/off | EP model key/action noises (Waves Crate) | Tine EP, Reed EP, Pad Reed EP, Clav |
| `sax-keyclose` / `sax-keyopen` (120 each, per fingering, 4 RR) | on / off | Bear Sax pad clicks (CC0) | all 7 saxes (Bear: its own per-note map); flute, clarinet, oboe, bassoon (−4 dB) |
| `breath-puff` (6) | on | Bear Sax + War Tuba breath (CC0), shaped 0.42 s puffs | woodwinds, saxes, recorder, ocarina, brass ×5, Male Aah |
| `bow-start` (12) | on | String Cyborgs bow-screech recordings (CC0) | all bowed strings (not on pizzicato/harmonics), Bowed Bass Pad |
| `guitar-pick` (12) / `guitar-fret` (5) | on / off | Emilyguitar muted-pluck transients / fingering noise (CC0) | electric/nylon guitars, ganjo, Jazz Bass; fret-only on Archtop and Jazz Pizz Bass; harps/zithers/lyre get fret at −40 dB |
| `wood-knock` (8) | on | bigcat-cello body knocks (CC0) | marimba, xylophone, balafon |

Levels (`relDb`) are the noise's loudest 100 ms (K-weighted) relative to the calibrated note at velocity 100, so Noise 0.5 = these, 1.0 = +12 dB: key-down −32, key-up −30, EP key −30/−32, breath −30 (flute) … −36 (voice), sax clicks −34/−32, bow −30, pick/fret −34, knock −30. They sit near Salamander's own authored release-noise level. No cleared **brass valve** noise exists (gap).

### 9.4 Tuning correction `tfix`
`analyse.measure_f0`: YIN (±150 ¢ search, k-period refinement), then the frequency of the fundamental **partial** from a Hann FFT over the same span, which is what a tuner reads and is immune to piano inharmonicity. `tfix = −(measured deviation from ET at the root + authored fine tune)`, applied by the runtime only when Tuning = Equal. Rules: one value per note and RR take (median over velocity layers, so a hard-struck string's natural sharp start is kept); unreliable → 0 (no periodicity, IQR > 12 ¢, partial/YIN disagree > 10 ¢, < 3 frames, or > 30 ¢ without ≥ 5 steady frames); whole-semitone transposes are never corrected; a sample a whole semitone off its root (±25 ¢) gets its root fixed (outside mallets/percussion). Timpani and tubular bells are `unpitched`.

- **Salamander "Natural" keeps its stretch.** A cubic stretch curve is fitted to the measured pitches (A0 −11.8 ¢, C4 −1.2 ¢, C8 +34.1 ¢: a normal Railsback shape) and only each note's deviation from that curve is corrected (median 1.7 ¢). Flattening it to ET would make the octaves sound flat to the ear. "Retuned" is corrected to ET (worst 19.6 ¢ at D#2, median 2.6 ¢).
- **Root fixes** (sample ≈ 1 semitone off its declared root): Salamander C8 (+99 ¢, its "C8" file is a C#), MTG baritone C2/D2 (+98…100 ¢), Hungarian zither ×5 (+90…122 ¢).
- **Largest corrections** (worst note per articulation): ganjo −56 ¢ (the whole instrument is ~40 ¢ sharp), erhu sul tasto −55 ¢ / short −47 ¢, Bear Sax staccato −49 ¢, old player piano −43 ¢, clarinet staccato +39 ¢, dan tranh −38 ¢, hang −38 ¢, War Tuba staccatissimo −36 ¢, Osiris −29 ¢ (top), VSCO tuba +30 ¢, Knight upright +22 ¢, Kawai +15 ¢, Steinway B −19 ¢, VSCO violin section +8 ¢ (sustain; the "−18 ¢" first estimate did not reproduce). EP models: |tfix| < 3 ¢.
- Per-instrument detail: `Tools/organics/library-report.md` (tp105 section) and each `build-report.json` → `tfix`.

### 9.5 Loudness normalisation
Every instrument is calibrated so its centre key (middle C when playable) at velocity 100, Velocity 0.75 (the runtime default) reaches **−24.0 LUFS** (BS.1770 K-weighting, ungated, first 1 s from the onset), with the same key at velocity 127 peaking ≤ −1 dBFS. One gain offset per instrument, so its internal velocity dynamics and register balance are untouched. Before: the old −18 dB-RMS calibration measured −7.4 (hang) … −23.8 LUFS (Clean Electric), a 16 dB spread. After: **−24.00 LUFS for all 74 (spread 0.00 dB)**, no instrument peak-limited. −24 is the loudest target that every centre-key crest (up to 23 dB on plucked strings) allows. Velocity-127 peaks elsewhere in the range are reported (`peak127RangeDb`) but not flattened. **tp113 (§11): −24 LUFS is now LIBRARY UNITS — the engine adds a flat +20 dB at its output, so the plugin plays the calibration point at −16 LUFS, the Wavetable init patch's level, and the calibration is no longer peak-limited.**

### 9.6 Round-robin and audibility audit (Max's xylophone report)
The compiler now drops RR steps whose own segment never rises above −50 dBFS and completes every RR set (`repair_rr`: missing sequential positions cloned from a sibling, random slots widened to cover [0, 1)). The test checks all 127 velocities × every key × every articulation. Result: 0 silent regions anywhere; 3 missing sequential positions repaired in the Yamaha upright; random-slot gaps closed in French horn (5), trombone (7) and solo violin (12). **`vcsl.mallets.xylophone` has no round robins at all** (1 region per zone and layer, every sample audible), so its "every other press is silent" cannot come from the data. The likely cause is the runtime's fake-RR neighbour borrowing near the range edge, which is Agent E's area.


## 10. tp108 (2026-09-25): per-key peak trim and short-take tuning

**Pipeline order for a compiled library** (each step closes through the real runtime, `organics_audit`):
`torgc.py` → `retune.py` (tfix, closed through the engine) → `engine_calibrate.py` (−24 LUFS) → `peaktrim.py`
(v127 peaks). A tfix change moves the calibration a little (two crossfaded takes beat differently), so the calibration
always follows the retune, and the trim always comes last.

### 10.1 Velocity-127 peaks: `Tools/organics/peaktrim.py`
Before: through the engine, measured at every round-robin take and with Noise at its default 0.5, **421 keys in 34
articulations of 24 instruments** peaked over −1 dBFS at v127 (flute top +9.6, Jazz Pizz Bass +9.1, mbira +8.9, VSCO horn
staccato +8.6, balafon +5.2, dan tranh +4.7, Hollowbody Guitars +4.3). After: **0 of 6,410 articulation × keys over
−1 dBFS**; the loudest key in the library is −1.01 dBFS (the EP Clav, untouched).
- The trim is per KEY and per articulation, only where a key is hot: the need is `min(0, −1.3 − peak)` (0.3 dB margin),
  and the curve is the largest one under every need that steps ≤ 1.4 dB from key to key (a ramp into the hot keys, the
  rest of the register untouched). Release and mechanical-noise regions move with their key. Never a limiter or a clipper.
- A region whose keys need different trims is split into one region per run of equal trim (same sample): 12,993 →
  15,851 regions in the library. Side effect: on a split zone with no round robins, Human's fake-RR borrow can pick the
  same sample (a twin) instead of a neighbour zone for that press.
- The calibration key keeps −24 LUFS unless its own loudest take is over the bar: then the trim is a peak limit on the
  calibration, recorded as `loudness.peakLimitedDb` (+ `peakLimitedByTrimDb`) exactly like engine_calibrate's own limit.
  Seven instruments are now under −24 by it (engine-measured, each within 0.05 dB of its recorded target): mbira 3.64 dB
  (its register is ±7 dB uneven: key 60 −6.5 dBFS, key 66 +8.9; the ≤ 1.5 dB step rule cannot ramp that away), Clean
  Electric 1.82 (1.53 of it from before), Kalimba 1.42, Ganjo 1.34, Hungarian zither 0.95, water glasses 0.56, timpani 0.33.
- Recorded in build-report `peakTrim` (curve per articulation, worst before/after, regions split). Bars:
  `organics_compile_test.py` §7 (curve ≤ 0 dB, ≤ 1.5 dB key to key, calibration key consistent, and the peaks re-measured
  through the engine) and `organics_audit.sh lib` (a hot key is now a FAIL, no longer a FLAG); `organics_audit.sh peaks`.

### 10.2 Tuning: `Tools/organics/tuning.py` + `retune.py`
`tuning.measure_pitch` replaces `analyse.measure_f0` in the compiler: multi-window YIN + MPM over the steady part after
the attack, the fundamental partial and a harmonic-template fit (stiff-string B on struck / plucked) for 40 ms takes,
searches confined to ±150 ¢ (no octave jumps; a sample an octave off its root still measures its cents). Performed
notes use one continuous blended estimate that leans on the settled half of a scooping take. `tuning.assign_tfix` keeps
the tp106 rules and adds the inheritance: a take the detector still cannot trust takes its note's other takes, else the
same key (≤ 3 keys) of a sustained (looped) articulation of the same session (`"tfixSession"` in a recipe splits
sessions), else its measured neighbours. tfix may reach ±95 ¢ (the runtime clamps ±100) from a strong reading only — the
old player piano's A0–B0 sound 60–70 ¢ flat. A semitone-mislabelled sample moves its whole note (Salamander C8, every layer: recipe `"rootFollow"`); an authored fine tune that already takes back most of the offset (Kawai A0: +81 ¢ on a file 113 ¢ flat) is a correction, not a mislabel.
`retune.py` re-measures a compiled library and then CLOSES THROUGH THE ENGINE: every key rendered (struck: 40/80/120;
performed: every layer at its centre) and each region moved by the residual of the notes it played.

Pitch check (`Tests/organics_pitch_check.py`, now judging every performed take at its layer centre, not only v80):
before **188 of 4,875** measurable keys off by > 5 ¢ (1,317 keys unreadable); after **93 of 5,921** (1.6 %), **0 over
30 ¢**, 271 unreadable (each listed with its reason by the check). What remains, and why it cannot reach ±5 ¢ with one
correction per sample:
- the same take reads differently at two velocities or across the keys of its zone (a pitch that moves inside the note:
  tremolo, pizzicato, staccato, a player's scoop): VSCO strings tremolo / pizzicato (cello section 6, contrabass 8, solo
  violin 8, viola 4, violin section 3), Meatbass 10 (up to −27.2 ¢ at pizzicato k66), VSCO woodwind / brass staccato
  (clarinet 3, oboe 4, horn 1, trumpet 2, tuba 2), Bear Sax staccato 6, recorder / ocarina vibrato 1 each, erhu 1;
- beating ensembles and reed pairs: the accordion's musette reeds 24 (k0–17 are its lowest sample repitched down
  several octaves), the synth choir's chorus 5 (−7.1 ¢ on k98–102 of a zone that reads in tune at k103–108);
- plucked / struck notes whose pitch glides with the strike: dan tranh vibrato 2, Hollowbody 1,
  Yamaha upright 1 (k33, a bass string whose fundamental is 20 dB down).
Unreadable (not judged): mostly Jazz Pizz Bass (50: no clear fundamental on the low pizzicato), string tremolo/sustain
with period and partials disagreeing, and short breathy takes. Salamander "Natural" keeps its fitted stretch (§9.4).


## 11. tp113 (2026-09-25): the Organics play at the synth's level

Max: "Why are the Organics so quiet? Normalize them and turn them the fuck up … each one damn near at the same level."
Measured through the real processor (default osc Volume 0.5, default master, C4 v100, K-weighted, first 1 s):
Wavetable init −15.8 LUFS · FM −17.2 · Organics −36.0 (median of 74; mbira −39.0). The library's −24 LUFS sat 20 dB under the synth.

- **One global gain:** `organics::kOutputMakeupDb = 20` (`OrganicsApi.h`), applied to the engine's summed output (attack,
  release and noise regions alike: no balance change; no limiter, no clipper). The library stays in LIBRARY UNITS
  (−24 LUFS / −1 dBFS at unity); every engine-level bar = library bar + 20. The visualiser level stays in library units.
- **Outliers:** `engine_calibrate.py` no longer peak-limits. The seven instruments tp108 held under −24 were lifted
  (mbira +3.64, Clean Electric +1.82, kalimba +1.42, ganjo +1.34, Hungarian zither +0.95, water glasses +0.56, timpani +0.33;
  `loudness.peakLiftDb`); `peaktrim.py`'s bar moves with the lift (idempotent, nothing re-trimmed).
- **After:** Organics median −15.9 LUFS through the processor (= Wavetable), engine calibration point −4.00 ± 0.05 for all 74;
  processor spread 3.0 dB (66 of 74 within ±1 dB; the rest are short mallets/percussion +1.2…+1.7 and the hang −1.3).
- **Peaks (headroom traded for level, as asked):** v127 at the plugin output, every key × RR take (6,410): 35 % over 0 dBFS,
  19 % over +3, 9 % over +6, max +10.4 (mbira); v100: 18 % over 0, max +7.1. In a DAW the master passes float (no clip stage;
  `masterGuard_` is standalone-only) — the DAW's own master decides. The standalone app's limiter + soft clip catches them.

## 12. tp114 (2026-09-26): the Organics safety limiter

Max: "yes build the organics limiter — let's hear how that would sound … I don't want quality or volume changed … find a way
to make these NOT clip." An explicit, recorded exception to the lifeguard law (never a limiter) for ONE stage: the Organics
engine's output after the +20 dB makeup (`OrganicEngine.cpp` PeakGuard, `OrganicsApi.h` `kLimiterCeilingDb`).

- **The path, measured:** downstream of the engine a voice applies osc Volume × pan × the amp envelope (5 ms attack, 200 ms
  to 0.7 sustain) × the unison norm; the −6 dB pre-FX pad and the ×2 instrument makeup cancel. At the envelope's top on the
  default osc (Volume 0.5, centre pan) that is −9.03 dB — so tp113's "35 % over 0 dBFS, max +10.4" (engine − 11.9 dB, the
  sustain level) under-read the attack: at the plugin output v127 had **50 % of 6,410 keys over 0 dBFS, max +13.3**
  (mbira), v100 32 %, max +10.0.
- **Design:** per engine (= per osc per voice), stereo-linked, **zero added latency** (it reads ahead inside the chunk the
  engine has already rendered: 1.33 ms min-hold + box ramp, 30 ms hold, 40 → 150 ms program-dependent release, exactly 1.0f at
  rest — bit-identical under the ceiling), **true peak** (a 16-tap 4× estimate held 0.5 dB under, so a BS.1770-style meter
  reads ≤ 0 dBTP), a 0.5 dB working margin inside an episode (a peak that lands in a chunk's first 1.3 ms up to 0.5 dB louder
  than the last needs no fast drop), and a **voice-aware ceiling**: the voice passes the block's largest downstream gain
  (`OrganicParams::outGain`), so ONE Organics osc leaves its voice ≤ −1 dBFS at any Volume / pan / envelope and a quieter
  Volume never limits. Without it (the engine alone) the default path is assumed (+8.03 dBFS at the engine).
- **Proof:** `organics_audit.sh peaks` (6,410 keys × every RR take × v127 / v100, Noise 0.5): 0 over; limited max −1.00 dBFS,
  4× ISP max −0.47 dBTP (output-referenced); the library's own PEAK lines unchanged (byte-identical). `lib`: 74/74 factory
  clean (the v127 bar is the ceiling); 1 user import (user.piano k33 v127) gains a blind-spot click (a +7 dB spike 10 samples
  into a chunk). `organics_null` 54/54 bit-identical; engine test 106/106; integration 46/46.
- **The cost (the honest part):** the hot keys are hot in their BODY, not just the transient — the calibration point at v100
  is untouched on 47 of 74 instruments, but 27 lose loudness (meatbass −6.2 dB, balafon −6.0, ganjo −5.5, kalimba −4.4,
  timpani −3.9, steel pan −3.4 … engine-level, default path). Hottest notes (mbira k52 v127: 14.8 dB GR for ~0.65 s; tuba k32
  v127: 11.2 dB for 1.7 s) are audibly turned down; residual after a 5 ms gain match −30 … −42 dB (no distortion to speak of).
  Two limited oscs on one note, or a chord, still sum over 0 dBFS (+4.5 dBFS measured) — no master clipper was added.
- CPU per engine per 512 block: +0.1 µs at rest (one peak scan), +7.7 µs while limiting; 0 when the engine is idle.

