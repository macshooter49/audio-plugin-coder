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
| `mod_src_cert.cpp` | fb563 — the six new SOURCES reach the audio on the installed AU (macro · wheel · aftertouch · bend · random · alt), the door drops unknown codes, depth sign, a MACRO AS A DESTINATION (wheel → Macro 1 → Level A; fb565), Macro 9 on wire 228, **fb566** a macro (and the whole wheel → macro chain) into the RACK's reverb mix, the Free LFO RUNS while a note sounds and PARKS across silence (A/B), the connection curve, bypass, scale by. Level A at ZERO so the source is the only way to a sound. **fb572** bar 7b: two Random routes (Level A, Coarse A) over 24 notes draw INDEPENDENTLY (one Random; read r = +1.00 before). | **fb573** bar 14: an LFO on a POINT of a connection curve moves the level (two windows differ; amount off → match; the parked bank re-bakes nothing). | 28 |
| `macro_rename_gate.js` | fb569 — right-click a macro -> Rename works EVERY time: a RIGHT-button press on the name grip arms no drag (the phantom drag that ate the Rename click is gone), the LEFT-button drag still works, the menu carries Rename, and Rename enters edit mode. Mutation: guard removed -> bar 1 red. | 6 |
| `filter_mod_cert.cpp` | fb568 — a MODULATOR MOVES THE FILTER CUTOFF on the installed AU: macro · wheel · aftertouch · random each move Cut1, random SCATTERS it per note (Max's ask); LFO unbroken; the dry-send trap. The per-voice cut gather knew LFOs only. | 9 |
| `midi_learn_cert.cpp` | fb563 — a learned CC moves its parameter on the installed AU; the map installs and round-trips through the state; CC 1 bound to a knob still drives the wheel source. | 7 |
| `lfo_park.js` | fb567 — THE LFO PLAYHEAD in the plugin: rides the pushed phase while notes sound; NEVER creeps on a stale feed (the pre-fb567 painter simulated 342 px/s of motion in silence); fades out and RESTS (0 writes) in silence; rAF mode (the popped card) stops and restarts; a DEAD feed parks it. **fb570** bars 9/10: the fade-out runs on ITS OWN .35 s curve (still > 20 % visible at 150 ms — fb567's idle rule had lost `transition` to the base rule by specificity, a 120 ms fade that read as a click) and the curve's breathing is PAUSED, not removed. Mutations 1-3. | 14 |
| `mac_idle_frames.mm` | fb567 — the INSTALLED AU, its real editor (windowless, fb521), audio rendered at real-time pace on a thread: frames the editor SHIPS per second at idle (≤ 6 — measured 0), with a note held (≥ 20 — 59), and at idle again after it (0). Reads the plugin's own beacon (`terrain-cpu.txt`, on the Mac since fb567, opt-in), whose DSP / UI lines are the Mac editor-open numbers. Before fb567: 53 / 54 / 59 — the loop never stopped. | 5 |
| `crv_popout_gate.js` | **fb570 — EVERY CURVE HOST POPS OUT.** Main page (a recording Juce stub): a warp curve's ⧉ parks its IDENTITY (`setCardState('crv', {warp,osc,slot})`) BEFORE `popOutCard`, no refusal toast, the docked copy hides; the SAME host re-opened is only re-fronted; opening a MOD curve while it floats RETARGETS (`setCardState({mod,s,d})` + `retargetCard`) with NO docked twin; Dock reopens the host it left with (mod → warp slot 2 → the Distortion), by identity alone, placed by the editor (never a body anchor at 8,8); one relay kick merges a curve the wire carries and Dock opens on it. Card-only page (`?card=crv`, relay-less like the real card window): a warp identity boots the warp host with exactly ONE 300 ms follow-poll, a point edit CAPTURES through `getParamCardinality`'s cache (`SYN_OSC_A_WARP_MODE` → 37, `setWarpDrawCurve` 129 samples) and the poll stands down, the Distortion lanes asleep; a mod identity boots the mod host and its edit lands in `setSynthMod` with `c`; a mod route with NO surface on the card page (LFO 3 Depth while tab 1 is active) still boots and its edit carries the WHOLE matrix (the review's blocker: the old mirror deleted every route it could not draw); a host that cannot mount, or a route removed from the patch, CLOSES ITS WINDOW (never a blank always-on-top window with no ✕); no identity = the Distortion. Mutations 1-5 (the fb560 toast back · identity not parked · the fb559 guard · no cardinality cache · the mirror's no-surface bail back). | 16 |
| `card_natives_gate.py` | **fb570 — THE TWO NATIVE LISTS AGREE.** Every native the curve editor, its warp/mod hosts, the mod mirror and the card-only boot call (read out of index.html, never typed here) is registered on `TerrainCardWindow`'s OWN list — the fb328/342/343 law made structural (a main-only native never settles its promise in a card window: no error, an empty field, a leaked poll). `CNG_MUTATE=drop` reds it. | 29 |
| `fb570_popout.js` | **fb570 — THE REAL POP-OUT** (a `curve_probe.mm` gesture, installed AU, real editor, real second window): ⧉ on a warp curve → `getPoppedCards` says `crv`, the identity is parked, and the POPPED PAGE reports what it booted (`getCardState('crvBoot')` = `{key:'warp', title:'Fractalize · OSC A · WARP 1 — Curve'}` — the second WebView is otherwise invisible to every harness); a mod curve opened while it floats → the popped page reboots as `LFO 1 — Curve`; Dock → the docked card comes back on that host. `CP_SETTLE=12000 CP_REPORT=<scratch>/report.js /tmp/curve_probe Tests/fb570_popout.js`. **Reports, does not gate** (read the trace). | — |
| `curve_apply_cert.cpp` | **fb572 — A CONNECTION CURVE ON A ROUTE YOU ALREADY HEAR.** Installed AU: for Env→Fold, LFO→Fold, Env→Level, Key→Level, Random→Level, Macro→Level and Macro→Reverb Mix, a straight / inverted / zero curve in FRESH order (route + curve in one push) and in MAX'S order (the route first, the curve drawn on it after). Before fb572 every per-voice destination was INERT in Max's order (modCfgEq never compared `curve`, so the voices kept a stale index) and a route could read ANOTHER route's curve after a shift; the global pass and the rack were fine. | 22 |
| `rand_hash_gate.py` | **fb572 — ONE RANDOM, ONE HASH.** The page's `randForRoute` twin and `SynthModConfig.h`'s are run over 512 (seed, dest, ordinal) triples (seeds spanning 0, 1, 2^31, 2^32−1) and must agree bit-exactly — the comet is a display of the DSP's draw. | 1 |
| `crv_ptmod_gate.js` | **fb573 — AN LFO ON A POINT OF A CONNECTION CURVE.** Modulating a point by LFO 2 sends `p` (points + {ys,ya}) beside `c` on the route; the editor reopens from the points with the mod; an LFO value pushed through the frame feed LIFTS the modded handle and rewrites the ink (−1 lowers it; a stale feed restores the static drawing); Off drops `p`, and a straight base drops `c`. Mutations 1-2 (no painter · no `p`). | 5 |
| `mark_air_gate.js` | **fb571 — THE MODULATION MARK KEEPS AIR UNDER ITS WORD.** Max: "it looks way too close and cluttered... put it down a little, one or two pixels." The line sat 2.5 px under the baseline on every surface (a pixel under the letters, touching a descender); now 4.5 px under the ink on every knob word (+2 on the 8 px words, +3 on the rack's 7 px word), the envelope shelf chips unchanged. Also: the macro mark spans the WORD (the word selector did not know `.vm-ml`, so it measured the knob+label wrapper), and the macro labels are the synth page's 8 px ("Baby" read smaller than "Macro 2"). Bar 4 measures the room under every rack mark on both faces (tightest: the Utility back panel, 3.0 px). Mutations 1-3. | 6 |
| `macros_still_gate.js` | **fb574 — THE MACROS TOGGLE MOVES NOTHING.** Max (with a screenshot): "every time we press macros, it does that padding and that resize again, which... fucks up with our LFO visualization... Nothing can move." Measured: the bottom row is content-sized and the voice column swapped its two views with display:none, so the row, the rack and every device took the height of whichever view showed (Chromium 187 → 159 px; the real WebKit page 187 → 182), and the marks — painted on the frame clock, asleep at idle — stayed where the old layout had put them (7 px under the Mix word, then 3 px INTO it). Now both views sit in one grid cell, the inactive one visibility:hidden. Bars: real clicks off → on → off keep the row, the rack, the device, the WT Pos word and the Mix word byte-identical; the marks hold with no frame ticked; both views share one rect; a routed macro's mark shows only with the Macros view (2 → 3 → 2); Mono moves nothing either; (fb575) 40 ms after each click the Always/Scaled pills carry their view's visibility, the hidden view is opacity 0 and the pills transition colours only — the real WebKit page never ended the old `transition:.14s` (= all) on the inherited visibility, measured 3 s+. `MACROS_STILL_MUTATE=1..4` prove the bars can fail. |
| `macro_face_gate.js` | **fb575 — THE MACRO FACE RIDES THE FRAME CLOCK.** Max: "I move my mod wheel on the Macros, it's still choppy." Measured: 8 face repaints in a 1 s sweep fed at 60 fps (a 150 ms poll). Bars: 60 shipped frames carrying the knob 0 → 1 repaint the face >= 45 times with no gap over 40 ms; a drag keeps the feed off the face. `MACRO_FACE_MUTATE=1`. |
| `macro_wheel_cert.cpp` | **fb575 — A MACRO UNDER THE WHEEL IS AS SMOOTH AS UNDER THE MOUSE** (installed AU). CC 1 → Macro 1 through `midiCcMap`, Macro 1 → Level A through `synModJson`, Level A's knob at 0. A held note while the wheel sweeps 0 → 127 one CC per block with the run loop pumped (the timer fires as in a host); the envelope's biggest 1.3 ms jump and longest plateau, wheel vs the same sweep through AudioUnitSetParameter. Before: 1.60 % / 45 ms against 0.67 % / 9 ms. Bars: the wheel no worse than the mouse; neither path steps (<= 1.5 %, <= 24 ms); the parameter follows the wheel; a mouse move after the wheel wins. |
| `macro_rename_gate.js` | **fb574 — A MACRO CAN BE RENAMED (MENU + DOUBLE-CLICK) IN THE HOST.** Max: "I'm trying to right-click, press rename, and it's not working... make it so that we can double-click on the name... the letters have to be the same size." Why: arming the fb135 bridge makes the C++ (editArm) grabKeyboardFocus(), WebKit blurs the field, and its blur→commit closed it before a key landed (fb136's preset rename already had no blur handler: "survive the arming blur"); measured on the installed AU, a late names seed (paintNames) also rewrote the word over the open field at +400 ms. Bars: the real menu path opens the field on the bridge; it survives a blur; B·a·b·y·Enter through `__tiHostKey`; a real double-click renames; a click elsewhere commits, Esc reverts; the field's computed type equals the label's (8 px); a repaint of the names leaves an open field alone. `MACRO_RENAME_MUTATE=1..4`. |

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
