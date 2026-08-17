# Tests — committed harnesses

Every build bible in `Design/` cites harnesses by paths that do not exist: they were written in
per-session scratchpads under `/private/tmp` and evaporated. These do not.

```sh
cd plugins/TerrainInstrument
clang++ -std=c++17 -O2 -Wall -o /tmp/fxtopo_test Tests/fxtopo_test.cpp && /tmp/fxtopo_test
clang++ -std=c++17 -O2 -I Tests/shim -I Source -o /tmp/flt_cert Tests/flt_cert.cpp && /tmp/flt_cert
```

| Harness | Covers | Gates |
|---|---|---|
| `fxtopo_test.cpp` | `FxChainTopology` — per-osc routing, merging, the serial chain | 135 |
| `flt_cert.cpp` | `FilterFxEngine` — unity, mix law, engine distinctness, every knob, stability | 34 |

`shim/` is a ~20-line stand-in for the four JUCE symbols `TerrainFilters.h` actually uses
(`jlimit`, `jmin`, `jmax`, `MathConstants`). That is the whole reason the 94-engine filter core
can be certified offline without building the plugin.

## The law these cannot enforce

**A green DSP harness proves the ENGINE works. It never proves the plugin REACHES it** (fb373 —
Cassette ran Studio's machine, bit for bit, through four rounds of green measurements). The
UI→param→DSP round trip is gated separately and headlessly.

## Probe craft, learned the hard way in this file

* **The probe matters more than the metric.** A cutoff sweep measured on a bass-heavy chord reads
  flat, because the source has no top end to remove. Use noise for a response.
* **Give the probe headroom in both directions.** A 3 kHz probe under a 159 Hz cutoff already sits
  at −139 dB, so "closes further" fails on a filter that works.
* **Centroid inverts at the closed end** — once a filter shuts hard the survivor is measurement
  residue, which has a *high* centroid.
* **Measure what the control means.** Poles means slope, so fit dB/oct; centroid barely moved.
* **Every gate self-checks.** If a metric cannot see an injected fault it is decoration.
