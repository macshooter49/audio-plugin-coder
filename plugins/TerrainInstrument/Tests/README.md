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
| `fxtopo_test.cpp` | `FxChainTopology` — per-osc routing, merging, the serial chain, the 50-device deep chain | 143 |
| `flt_cert.cpp` | `FilterFxEngine` — unity, mix law, engine distinctness, every knob, stability | 34 |
| `send_mode_test.cpp` | fb414/415 SEND — which oscillators leave the main mix, and the first-slot law | 19 |
| `fx3_sync_test.cpp` | the SYNC LAW across chorus + flanger + phaser AND the UI's own label | 21 |
| `fx3_ui.js` | the fb413 cards: 6 instances each, route pills, the strobe law, no-doubles, the C++ push | 48 |
| `grn_click_audit.cpp` | fb416 granular — overlap/duty, the slope detector, the FFT band. **Reports, does not gate.** | — |
| `fx3_audibility.cpp` | fb417 "can you hear it" — spectrum distance knob-0 vs knob-100. **Reports, does not gate.** | — |

The three fx3 engine certs live beside their engines, not here — they need the roster and the
bibles next to them:

```sh
for d in chorus flanger phaser; do
  clang++ -O2 -std=c++17 -I Tests/shim -I Source -I Design/fx3/$d \
          Design/fx3/$d/${d}_cert.cpp -o /tmp/${d}_cert && /tmp/${d}_cert
done            # chorus 87 · flanger 83 · phaser 67
```

⚠️ **Run those three ONE AT A TIME.** In parallel the flanger's BBD CPU gate reads 25.20 µs
against its own 25 µs bar; alone it is 23.07. That is contention, not a regression, and it has
cost a diagnosis once already.

⚠️ `fx3_ui.js` needs puppeteer-core, which is not vendored here:
`NODE_PATH=<a scratchpad>/node_modules node Tests/fx3_ui.js`

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
* **Run the new gate against the OLD code.** Every gate added in fb413–418 was first pointed at a
  deliberately-reverted copy and required to FAIL. The deep-chain test failed 2/2 on the uint32
  feed mask; the no-doubles/scale test failed 3/3 on the old dropdown writer. A gate that has
  never failed has never been tested.
* **An OUTLIER detector is blind to a CONTINUOUS fault** (fb416). `max|Δx| / p99.9|Δx|` finds a
  splice. The granular's micro-clicks were a 25 % duty cycle — the artifact was in the BULK of
  the distribution, not its tail, so the number never moved however bad it got.
* **Geometry is not hearing** (fb417). Bounce measured "57.8 % departure from the un-bounced
  path" — a true statement about a control signal nobody listens to, on a knob Max could not
  hear at all. Measure the OUTPUT spectrum, and print it beside knobs the user already agrees
  are obvious, or the dB has no scale.
* **Check your own detector before believing it.** A one-pole HP at 6 kHz leaks a 220 Hz sine at
  −28.7 dB, so it reported "−32 dB of HF" on every case — below what the bare probe scores. A
  detector that reads the same on clean and dirty is worse than none.
