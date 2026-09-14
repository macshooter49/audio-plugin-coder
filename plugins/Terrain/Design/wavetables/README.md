# THE TERRAIN FACTORY WAVETABLE BANK — the generators

## fb638 — THE FIVE HUNDRED, AND NO MORE "TERRA" (2026-09-13)
Max: *"I want 500 total factory wave tables … no more Terra when it comes to the name … just have the name of the wave
table."* And, mid-build: *"Riot's banks are EXTREME and RAW + they usually morph into something crazy … keep basic stuff in
there too (a little bit lol, aim for the crazy ones, even some subby ones, MAX quality and harmonics)."*

**500 = 46 built-ins + 454 FLAC** (the 120 from fb604a/fb612 + 334 new), in THIRTEEN categories: the ten plus **Abstract**
(shapes that make you ask what they are), **Processed** (one process swept across the frames — the Virtual Riot idea, built
our own way) and **Textures** (Max's own Waves Crate one-shots turned into tables). **Shipped (measured on the FLACs):** 454 tables · harmonics > -60 dB mean **439** (the new 334: 452; Serum 2 Xfer factory: 318) · centroid travel median **61.6 st** (Serum 2: 23.2) · crest median 2.43 · median distance to the nearest other table 9.31 dB, 15 % within 7.65 dB · worst FLAC round-trip -138.5 dBr · 235.6 MB (the new 334 are 174.4 MB). Per category: Chaos 58 · Digital 50 · Abstract 48 · Spectral 46 · Processed 42 · Metallic 30 · Vocal 30 · Analog 28 · Cinematic 28 · Harmonic 24 · Physical 24 · Textures 24 · Basic Shapes 22. Frozen-bank golden 0/52 static + automated; `Tests/wt_factory_gate.py` 10/10.

### The rename is preset-safe
`rename_factory.py --apply` git-mv'd the 120 files (`Terra - X.flac` → `X.flac`) — never re-encoded, so a preset's
`ref:1|<rel>|<hash of the file's bytes>` still matches — and generated `Source/WtFactoryAliases.h` (120 legacy → current
pairs) from `renames_fb638.csv` (with the md5s). The restore path maps a missing legacy path through it, upgrades the cached
ref and fixes a legacy display name. Proven: the frozen-bank golden renders all 52 of Max's presets bit-identical on a bundle
with no "Terra - " file left, and `Tests/wt_factory_gate.py` bar [8] resolves every factory ref in his 62 real presets.
The 16 built-in "Terra X" names lost the prefix too; the eight that would collide were renamed (Super Stack, Drift Chorus,
Duty Morph, Reso Climb, Vox Choir, Dense Cloud, White Grit, Snarl) — built-ins are saved by INDEX, so a label moves nothing.

### The toolchain (all in this folder)
| tool | job |
|---|---|
| `wtlib.py` | the contract + `shipping_name()` — now the plain Title Case name, no prefix (`legacy_shipping_name()` keeps the old form for the rename map) |
| `gen2_<category>.py` | the 334's generators, one designer per category (+ `_b` modules for the Textures and Processed refills) |
| `wtkit.py check <module>` | render, sanity bars (shape, NaN, DC, peak, dead frames, SEAM, static), nearest neighbours in-module and in the fixed set, contact sheets |
| `fixed_set.py` | fingerprints of the 120 shipped + 46 built-ins (dumped by `builtin_probe.cpp` from the plugin's own WavetableBank) + the **reference guard** |
| `gate500.py` | the selection: per-category quotas (`wtkit.QUOTA_NEW`), round-robin greedy, `--spacing` (dB between ANY two tables), `--exclude` (critique vetoes), `--prefer` (critique swap-ins), names unique against everything |
| `mkbank.py` | wav → 24-bit FLAC into `../../Resources/Wavetables/<Category>/` (only the bank it is given: the 120 are never touched) |
| `rename_factory.py` | the fb638 rename + `WtFactoryAliases.h` |
| `align_frames.py <bank>` | **no dropouts when the table is scanned**: Terrain crossfades adjacent frames, so anti-correlated neighbours cancel (a critic measured -18 dB holes). Each frame gets the circular shift + polarity that best matches the frame before — magnitude spectra untouched, fingerprint change 0.0000 dB — applied to any table whose worst midpoint dip is under -6 dB; `--check` fails the build if any table still has a worst dip under -9 dB or a median under -4.5 dB |
| `bank3_probe.cpp` / `bank3h_probe.cpp` / `fx_probe.cpp` | Terrain's own ModalEngine / HarmonicEngine / filters rendered for the Physical, Harmonic and Processed generators |

**Clean-room guard.** `ref_fp.npz` (built by the session's `ref_fp.py`) holds FINGERPRINTS ONLY — 768 numbers per table, no
audio — of the Serum 2 Xfer factory tables and DYNOX PLUTO 2. `fixed_set.load()` appends them as kind="reference", so every
new table must sit at least the spacing away from every reference table too. No third-party audio was ever read into a table.

**How the 334 were chosen.** 13 designers wrote ~600 candidates against PLAN (quotas skewed to the extreme categories, THE
JOURNEY RULE: frame 0 playable, the last frame wild). `gate500 --spacing 7.0` (the calibrated collision floor is 5.10 dB, a
gently re-tilted saw; 7.0 dB leaves real air between any two tables). Eight critics then looked at every selected table —
sheets, level curves, recipes — and vetoed the dull, the broken and the look-alikes (48 + Processed), renamed the awkward
ones and chose same-category swap-ins; module owners fixed the faults in the tables they kept (reversed journeys, level holes,
a grain-position freeze).

### Regenerating
Engine/FX-derived tables read probe dumps: build the probes (same recipe as `bank2_probe.cpp` below) and point
`TERRAIN_WT500` (or the per-module `TERRAIN_WT_DUMP3` / `TERRAIN_WT_FXDUMP` variables) at their output. Textures re-read Max's
own Waves Crate library from `~/Desktop/Waves Crate` (solo packs only — no collaborations, no Splice archive). As in fb612,
the SHIPPED FLACs are the source of truth: they are the exact tables that were auditioned and gated.


120 wavetables, 128 frames each, in **TEN categories** (fb606). **This directory regenerates them.**
The rendered `.wav` bank is NOT in git (120 MB); it is produced by running the pipeline below.

## fb612 — THE BANK NOW SHIPS
`../../Resources/Wavetables/` holds the same 120 tables as **24-bit FLAC** (61.1 MB, worst
round-trip **−138.5 dBr** measured across all 120), and `CMakeLists.txt` copies that tree into every
plugin bundle's `Contents/Resources/`. Installing the plugin installs the library — there is no
installer yet and this needs none. `wtFactoryRoot()` finds it from `currentExecutableFile` (NOT
`currentApplicationFile`, which for a plugin is the HOST app).

**Names are the shipping names now:** `TERRA BIT LADDER` → `Terra - Bit Ladder`. The rule is
`wtlib.shipping_name()` — ONE definition, used by `gate.py` when it renders the bank and by
`mkbank.py` when it converts it, so the two cannot drift. Generators keep their ALL-CAPS
identifiers; only the file on disk carries the pretty name.

```bash
python3 gate.py                                   # render the bank (now with shipping names)
python3 mkbank.py <bank-dir> ../../Resources/Wavetables --write   # -> 24-bit FLAC for shipping
python3 ../../Tests/wt_factory_gate.py            # 6 bars, 5 mutation controls
```

## fb606 — THE MERGED TEN
The bank used to ship eight generated folders while the browser showed a different set for its
built-ins, so the same sound had two names depending on where you looked. There is now ONE
taxonomy, and every generator declares which of the ten each of its tables belongs to —
`gate.py` reads `CATEGORY` and **refuses to guess from the module name**, because guessing is how
folders the browser had never heard of got created in the first place.

| folder | source | candidates -> shipped |
|---|---|---|
| Basic Shapes | `gen_foundation` first section | 12 -> 10 |
| Analog | `gen_foundation` second section | 14 -> 12 |
| Digital | `gen_digital` | 22 -> 14 |
| Vocal | `gen_vocal` (formant + granular halves) | 24 -> 14 |
| Metallic | `gen_cinematic` METALLIC/INDUSTRIAL section | 12 -> 12 |
| Spectral | `gen_spectral` (absorbs the old **Morph**) | 22 -> 14 |
| Chaos | `gen_chaos` (absorbs the old **Experimental**) | 24 -> 14 |
| Cinematic | `gen_cinematic` CINEMATIC/DRONE section | 12 -> 10 |
| Harmonic | `gen_engine`, HarmonicEngine renders | 12 -> 10 |
| Physical | `gen_engine`, ModalEngine renders | 12 -> 10 |

Six of the ten were already sections inside the generators — the split was in the source, it just
was not on disk. **No category ships empty**; `gate.py` checks and says so either way.

⚠️ **The two modal bells stay in Physical.** Filing BELLS METAL / BELLS GONG under Metallic is the
obvious move and it was measured and rejected: against twelve bright additive tables the selection
score cut both bells and Physical shipped 8 instead of 10. That is the quota failure below,
re-created inside one folder. See the warning in `gen_engine.py`.

## Where it is delivered
`gate.py` writes to `$TERRAIN_WT_OUT` when set, else `./bank`. The plugin reads
`<terrainDataDir()>/Wavetables/Factory/<Category>/` — on a machine with the legacy root that is
`~/Library/Application Support/WavesCrate/TerrainInstrument/Wavetables/Factory`
(`terrainDataDir()` prefers `WavesCrate/Terrain` and falls back to `WavesCrate/TerrainInstrument`;
see `PluginEditor.cpp`). `bank2_probe`'s dumps likewise honour `$TERRAIN_WT_DUMP`. Neither the
102 MB of dumps nor the 120 MB of wav belongs in the worktree — `.gitignore` covers both.

## Why this exists
Terrain shipped **46 factory tables at a hard 16 frames** with ~50 harmonics. Measured against the
references on the same metric:

| | tables | frames | harmonics >-60 dB | centroid travel |
|---|---|---|---|---|
| Serum 2 factory | 371 | median 20, max 300 | mean 278 | median 19.9 st |
| PLUTO 2 (3rd party) | 20 | mean 155, max 256 | 12 - 1023 | up to 48.6 st |
| Terrain BEFORE | 46 | 16 (hard cap) | ~50 | ~12 st |
| **Terrain THIS BANK** | **120** | **128** | **mean 404** | **median 59.0 st** |

The gap was never frame depth — Serum's factory MEDIAN is 20 frames. It was **brightness**
(278 vs 50) and **travel**. Both are now ahead of the reference.

## Run it
```sh
export TERRAIN_WT_DUMP=/some/scratch/dump2
export TERRAIN_WT_OUT="$HOME/Library/Application Support/WavesCrate/TerrainInstrument/Wavetables/Factory"

# 1. engine-derived material (needs the plugin headers)
c++ -std=c++17 -O2 -I ../../Tests/shim -I ../../Source bank2_probe.cpp \
    -framework Accelerate -o /tmp/bank2 && /tmp/bank2 "$TERRAIN_WT_DUMP"
# 2. build, measure, dedupe, select, write
python3 gate.py          # numpy only
```
`gate.py` writes `<out>/<Category>/<NAME>.wav` plus `MANIFEST.csv`, sweeps away any folder that is
not one of the ten, and prints whether each of those checks fired.

## The pieces
| file | what it is |
|---|---|
| `wtlib.py` | the shared harness — construction, `finalize()`, the measurement board, the uniqueness fingerprint, the WAV writer. **Every generator imports it** so all 120 are measured identically. |
| `bank2_probe.cpp` | renders from Terrain's OWN engines: HarmonicEngine's 512-partial bank across six spectral families, and ModalEngine across all NINE instrument families and all THREE cores (string waveguide, reed-bore, modal bank). **No other wavetable synth can ship these** — none has a physical model to render from. |
| `gen_engine.py` | turns those dumps into cycles (spec → cycles; modal render → harmonic projection) |
| `gen_foundation.py` | Basic Shapes (the known quantities) + Analog (the vintage-hardware lane) |
| `gen_digital.py` · `gen_spectral.py` · `gen_chaos.py` · `gen_cinematic.py` · `gen_vocal.py` | the algorithmic categories. `gen_cinematic` carries TWO of the ten (Cinematic + Metallic). |
| `gate.py` | builds all candidates, measures, **proves no two sound alike**, applies per-category quotas, writes the bank |
| `MANIFEST.csv` | every shipped table's category, harmonics, span, crest, zero-crossings |

## 🔑 Two things that are load-bearing — do not "simplify" them

**1 · The uniqueness threshold is MEASURED, not chosen.** `gate.py` calibrates against two anchors:
a saw vs a gently re-tilted saw (a real but musically trivial change) = **5.10 dB**, the floor; and
a saw vs a pure sine = **53.82 dB**, the ceiling. Two tables must be at least as far apart as the
floor. Result over 7 140 pairs (fb606 run): closest **5.30 dB**, median 23.33. An earlier invented threshold of
2.0 dB would have shipped a 4.96 dB pair as "distinct" while it was closer than a re-tilt.

**2 · The per-category quotas exist because a global quality score DELETED ALL 12 PHYSICAL TABLES.**
The score rewards brightness, and modal/waveguide sources are darker by nature — so the selection
was throwing out precisely the tables nothing else can make. Quotas also guarantee Basic Shapes
exists (a sine is one harmonic by definition and loses every global ranking). fb606 re-sized all
ten from their actual candidate pools; the protection is unchanged and was re-confirmed the hard
way when the bells were briefly moved to Metallic.

**3 · Three modal tables were colliding, and two of them are now fixed AT THE SOURCE.** BOW
PRESSURE, BELLS METAL and BARS GLASS were being cut every run — the bank shipped 9 Physical, not
12. Two reed-bore or two modal-bank families swept over the same ranges in the same DIRECTION read
as one table, because the fingerprint is order-sensitive and the parameters were near-parallel.
Reversing those sweeps (which is also a real musical gesture — a bow releasing, a bell closing)
clears BELLS METAL and lifts Physical to its full ten. BARS GLASS vs BELLS METAL stays at 4.55 dB
and is the bank's floor case: four documented attempts only moved the collision, and the cause is
the harmonic projection quantising away the partial-ratio detail that separates two dense
modal-bank geometries. `bank2_probe.cpp` records all four so nobody repeats them.

## Analysis-pitch gotcha (cost a whole run)
HarmonicEngine Nyquist-limits its bank to `nEff = 0.48·SR/f0`. Analysing at 110 Hz caps it at **192
partials** — the exact number we were trying to beat. Analyse at **SR/2048 = 21.53 Hz**, the frequency
whose period IS one 2048-sample frame, and the full 1023-harmonic grid is reachable (365-512 measured).
For ModalEngine the resonator needs a musical pitch, so it renders at **55 Hz**: halving f0 doubles the
harmonic-projection ceiling to ~400, which is what stopped the reed-bore families all pinning at the
same number and becoming indistinguishable.

## Output format
float32 mono 44.1 kHz, 2048 samples per frame, frames concatenated — the Serum/Vital layout, which is
what Terrain's own importer auto-detects (`PluginProcessor.cpp`: `n % 2048 == 0 && n/2048 <= 256`).
All 120 verified against that literal condition.
