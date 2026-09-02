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
| `lfo_park.js` | fb567 — THE LFO PLAYHEAD in the plugin: rides the pushed phase while notes sound; NEVER creeps on a stale feed (the pre-fb567 painter simulated 342 px/s of motion in silence); fades out in .35 s when no voice sounds and back in .12 s on the first; the painter RESTS (0 DOM writes over 60 dispatches); the rAF-mode loop (a popped card) stops in silence and a push restarts it. Mutations: 1 = the old painter (simulation, no rest) → bars 2, 4 red; 2 = no idle class → bars 0, 3, 6 red. | 11 |
| `mac_idle_frames.mm` | fb567 — the INSTALLED AU, its real editor (windowless, fb521), audio rendered at real-time pace on a thread: frames the editor SHIPS per second at idle (≤ 6 — measured 0), with a note held (≥ 20 — 59), and at idle again after it (0). Reads the plugin's own beacon (`terrain-cpu.txt`, on the Mac since fb567, opt-in), whose DSP / UI lines are the Mac editor-open numbers. Before fb567: 53 / 54 / 59 — the loop never stopped. | 5 |

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

## fb567 — the loop rests (2026-09-02)

Max: *"The LFO pauses, and then it just keeps going... Stop the animation loop when the MIDI is not
inside and there's nothing being played. Put the animation loop back whenever we are playing."*

`mac_idle_frames.mm` measured the shipped fb566 build first: **53 frames/s at idle, 59 after a
note** — the editor's loop had never once rested, so every migrated painter ran on a silent page.
Four causes, found in this order, each named by measurement rather than guessed:

1. **The page's LFO painter simulated.** `lfoFrame` kept fb217's fallback, "stale feed → `ph +=
   dt*effHz()`". fb511 makes the feed go stale at idle *by design* and fb566 parks the DSP LFO
   there, so the dot swept on the page's own clock while the DSP held. `lfo_park.js` bar 2.
2. **The legacy ModulationEngine bank.** Three front-page LFOs at a default 1 Hz, advanced every
   sample from prepare with no assignment in any patch, and their phases pushed every tick *ahead*
   of the quiet gate — so no idle frame was ever byte-identical. Now: pushed only with a live
   assignment and audible output; the engine skips the bank with nothing assigned.
3. **The hero scope's residue.** After a note the scope array printed `-0.0000` one tick and
   `0.0000` the next (±1e-6 leftovers). `SF()` now snaps anything under half the last printed digit
   to a clean zero, and the scope segment rides inside the quiet gate.
4. **The capture strip's seconds.** "Seconds available" grew every tenth of a second in silence —
   10 frames/s on its own. It rides only while audible, or when the export STATE changes.

**THE FRAME-DIFF PROBE.** Set `TERRAIN_CPU_PROBE=1` (or the beacon's marker file) and the editor
appends the first bytes that differ between consecutive shipped frames to
`<tempDirectory>/terrain-frame-diff.txt` (twice a second at most). Causes 3 and 4 were invisible to
code reading and took one run each to name. Zero cost when off.

**THE LAW.** An idle frame is a contract: everything appended outside the quiet gate must be
constant in silence, and a value below the page's printed resolution is zero. When idle frames
ship, run the probe — do not reason about which segment it might be.
