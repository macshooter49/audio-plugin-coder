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
| `wt_list_gate.py` | fb530 — the WAVETABLE ROSTER's **ten sites**: the enum, `specForPreset`, four `StringArray`s, four `<select>`s. Index for index, label for label. | 24 |
| `harmonic_ceiling_gate.py` | fb530 — **THE ONE LAW**: no generator may carry a harmonic-COUNT ceiling. The 25 capped legacy generators are FROZEN (the debt, written down); a new ceiling anywhere fails the build. | 8 |
| `fb563_menu.js` | fb563/fb564 — THE CONTROL MENU: every destination opens exactly one house menu; the Modulate picker (six families, multi-select, a double-click ASSIGNS and closes); route rows (tabular depth · curve · remove · bypass · scale by); registry rows; Reset; Copy/Paste; MIDI Learn; the codec; **fb564 THE GRID** (no header, no rules, one left rail, ⤢ · ✕ · › identical 16 px boxes on one right rail, ✕ red on hover); a macro's own menu (Rename · Reset · MIDI Learn, names persisted); Copy oscillator with modulators; a macro as a destination (fb565: nine macros, 3×3, Modulate › on a macro, its own picker hides itself). fb566: the hand-driven comet interpolates between feed pushes. Six mutations. | 54 |
| `fxmod_cert.cpp` | fb453 — the FX rack's modulation destinations and the SHIPPED per-block math (`FxModValue.h`, JUCE-free); **fb566** every family reaches the rack (macro · wheel · velocity · bend add, follower · Key own), Scale by and the connection curve apply, a source with no view is dropped. `clang++ -std=c++17 -O2 -I Tests/shim -I Source Tests/fxmod_cert.cpp` | 46 |
| `mod_src_cert.cpp` | fb563 — the six new SOURCES reach the audio on the installed AU (macro · wheel · aftertouch · bend · random · alt), the door drops unknown codes, depth sign, a MACRO AS A DESTINATION (wheel → Macro 1 → Level A; fb565), Macro 9 on wire 228, **fb566** a macro (and the whole wheel → macro chain) into the RACK's reverb mix, the Free LFO RUNS while a note sounds and PARKS across silence (A/B), the connection curve, bypass, scale by. Level A at ZERO so the source is the only way to a sound. | 22 |
| `midi_learn_cert.cpp` | fb563 — a learned CC moves its parameter on the installed AU; the map installs and round-trips through the state; CC 1 bound to a knob still drives the wheel source. | 7 |

The three fx3 engine certs live beside their engines, not here — they need the roster and the
bibles next to them:

```sh
for d in chorus flanger phaser; do
  clang++ -O2 -std=c++17 -I Tests/shim -I Source -I Design/fx3/$d \
          Design/fx3/$d/${d}_cert.cpp -o /tmp/${d}_cert && /tmp/${d}_cert
done            # chorus 87 · flanger 85 · phaser 67
```

⚠️ **Run those three ONE AT A TIME.** In parallel the flanger's BBD CPU gate reads 25.20 µs
against its own 25 µs bar; alone it is 23.07. That is contention, not a regression, and it has
cost a diagnosis once already.

⚠️ `fx3_ui.js` needs puppeteer-core, which is not vendored here:
`NODE_PATH=<a scratchpad>/node_modules node Tests/fx3_ui.js`

The two fb530 gates are pure python, no deps, run from the plugin root:

```sh
python3 Tests/wt_list_gate.py            # 24 checks across the ten sites
python3 Tests/harmonic_ceiling_gate.py   # 8 checks; prints the frozen legacy debt
```

Both carry their own MUTATION CONTROLS (fb421 — a gate that has never failed has never been
tested). Each of these must FAIL, and each does:

```sh
for m in enum case strD optD rename;  do WTLG_MUTATE=$m python3 Tests/wt_list_gate.py; done
for m in terra kernel legacy fixed;   do HCG_MUTATE=$m  python3 Tests/harmonic_ceiling_gate.py; done
```

**The floor is git's business (fb565).** `warp_menu.js`, `all_menus.js` and `flowmod_gesture.js`
measure a change against the page AS IT STOOD BEFORE it (a page error, a card footprint only
counts if the pre-change page did not already have it). They used to take that page from
`WARPM_PREPAGE` / `ALLM_PREPAGE` / `FLOWG_PREPAGE` and go red without it — three bars every
session stepped over as "pre-existing". `Tests/floor_page.js` now hands them git HEAD's
`index.html` when the variable is unset (the pre-change page for uncommitted work; on a clean
tree, the page itself). Set the variable only to floor against an older commit.

⚠️ `Tests/all_menus.js` now reads the wavetable count OUT OF `PluginProcessor.cpp` rather than
hardcoding it. It used to say 30; when the bank grew to 46 it reported a desync that did not
exist. A hardcoded roster length in a HARNESS is an eleventh site.

⚠️ **fb467 — anything that includes `Wavetable.h` now needs `-framework Accelerate`.** The bake's
transform moved to `Source/WtFft.h`, which calls vDSP where it exists (19.1 ms -> 2.3 ms per table)
and falls back to the shipped radix-2 where it does not. Affected today: `spec_cert.cpp`,
`wtfft_cert.cpp`, `au_spec.cpp`, `blur_align_audit.cpp`. `wtfft_cert.cpp` nulls the two backends
against each other over all 30 factory tables — it is the gate that says the swap changed the speed
and not the sound.

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
