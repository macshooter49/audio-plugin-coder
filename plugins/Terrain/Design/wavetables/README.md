# THE TERRAIN FACTORY WAVETABLE BANK — the generators

120 wavetables, 128 frames each, built 2026-09-08. **This directory regenerates them.**
The rendered `.wav` bank is NOT in git (120 MB); it is produced by running the pipeline below.

## Why this exists
Terrain shipped **46 factory tables at a hard 16 frames** with ~50 harmonics. Measured against the
references on the same metric:

| | tables | frames | harmonics >-60 dB | centroid travel |
|---|---|---|---|---|
| Serum 2 factory | 371 | median 20, max 300 | mean 278 | median 19.9 st |
| PLUTO 2 (3rd party) | 20 | mean 155, max 256 | 12 - 1023 | up to 48.6 st |
| Terrain BEFORE | 46 | 16 (hard cap) | ~50 | ~12 st |
| **Terrain THIS BANK** | **120** | **128** | **mean 420** | **median 60.9 st** |

The gap was never frame depth — Serum's factory MEDIAN is 20 frames. It was **brightness**
(278 vs 50) and **travel**. Both are now ahead of the reference.

## Run it
```sh
# 1. engine-derived material (needs the plugin headers)
c++ -std=c++17 -O2 -I ../../Tests/shim -I ../../Source bank2_probe.cpp \
    -framework Accelerate -o /tmp/bank2 && /tmp/bank2 <outdir>/dump2
# 2. build, measure, dedupe, select, write
python3 gate.py          # numpy only
```
`gate.py` writes `bank/<CATEGORY>/<NAME>.wav` plus `MANIFEST.csv`.

## The pieces
| file | what it is |
|---|---|
| `wtlib.py` | the shared harness — construction, `finalize()`, the measurement board, the uniqueness fingerprint, the WAV writer. **Every generator imports it** so all 120 are measured identically. |
| `bank2_probe.cpp` | renders from Terrain's OWN engines: HarmonicEngine's 512-partial bank across six spectral families, and ModalEngine across all NINE instrument families and all THREE cores (string waveguide, reed-bore, modal bank). **No other wavetable synth can ship these** — none has a physical model to render from. |
| `gen_engine.py` | turns those dumps into cycles (spec → cycles; modal render → harmonic projection) |
| `gen_foundation.py` | FOUNDATION (the basics, in their own folder) + ANALOG |
| `gen_digital.py` · `gen_spectral.py` · `gen_chaos.py` · `gen_cinematic.py` · `gen_vocal.py` | the algorithmic categories |
| `gate.py` | builds all candidates, measures, **proves no two sound alike**, applies per-category quotas, writes the bank |
| `MANIFEST.csv` | every shipped table's category, harmonics, span, crest, zero-crossings |

## 🔑 Two things that are load-bearing — do not "simplify" them

**1 · The uniqueness threshold is MEASURED, not chosen.** `gate.py` calibrates against two anchors:
a saw vs a gently re-tilted saw (a real but musically trivial change) = **5.10 dB**, the floor; and
a saw vs a pure sine = **53.82 dB**, the ceiling. Two tables must be at least as far apart as the
floor. Result over 7 140 pairs: closest **5.30 dB**, median 23.65. An earlier invented threshold of
2.0 dB would have shipped a 4.96 dB pair as "distinct" while it was closer than a re-tilt.

**2 · The per-category quotas exist because a global quality score DELETED ALL 12 PHYSICAL TABLES.**
The score rewards brightness, and modal/waveguide sources are darker by nature — so the selection
was throwing out precisely the tables nothing else can make. Quotas also guarantee FOUNDATION exists.

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
