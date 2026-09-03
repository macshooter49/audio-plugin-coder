# Serum 2's curve editors vs ours — what a "curve" is for, and where we stand

Written for Max. Plain language first; every claim about OUR code cites `file:line`
(paths relative to `plugins/TerrainInstrument/Source/`), every claim about Serum cites a URL.
Nothing in the source was edited.

---

## 0. The one-paragraph answer

A **curve** is a picture of "what comes out for what goes in". Left-to-right is the input
(a mod wheel from 0 to 100 %, an LFO from bottom to top, an audio sample from quiet to loud);
up-and-down is what the plugin actually does with it. A straight diagonal means "pass it
through unchanged". Bend it and you change the *feel* of a control without changing the
control. Serum 2 has exactly the same idea in three places (the LFO shape, the per-route
"Curve" in the Matrix, and the Warp "Remap" graph). We have it in three places too
(Distortion transfer, Warp Draw/Draw Amp, and the mod-connection curve), all on ONE editor.
Our point editor already matches or beats Serum's on gestures; the real gaps are a
**Smooth/slew** control, **independent X/Y grid counts**, a **saved-shape library**, and
**editor size / pop-out for the two guest hosts**.

---

## Part 1 — What Serum 2 does (researched)

### 1a. The LFO editor

Serum 2 kept the Serum 1 LFO graph and added to it. The Serum 1 manual (the gestures are
unchanged in 2, confirmed by 2025 Serum 2 guides) lists the graph gestures verbatim:

> "double-click to Add or Remove points · shift-click to draw steps at the Grid Size (step
> sequencer) · alt-click+drag Points to snap points to the Grid Size · alt-click+drag any
> Curve Point to move all curve points at once · click+drag on background to multi-select
> points · command-click+drag a point … to multi-select points for a relative movement
> (rainbow color …) · control-click (right-click) to bring up a pop-up menu … setting the
> segment 'shape' for shift-click, deleting all multi-selected points, or assigning the
> start or loopback points."
> — Serum manual, LFO Graph Display, https://s3.amazonaws.com/decembercymatics/Serum_Manual.pdf (p.35)

- **Tension** = the "Curve Point" in the middle of each segment; drag it up/down. Alt-drag one
  moves them all (same source; also https://www.productionmusiclive.com/blogs/news/10-quick-serum-tips).
- **Grid**: a number box; "The Grid exists to be used in conjunction with the Alt-click (snap
  points) or the Shift-clicking (draw step segments)". Serum 1 had ONE grid number, global to
  all LFOs. Serum 2 made X and Y independent: "Independent Grid — Independent X and Y setting
  for graph grid" (Xfer, *What's New in Serum 2*, p.13,
  https://static.xferrecords.com/Serum%202%20What's%20New.pdf); a user asked for "8 Horizontal
  and 12 Vertical" and Steve Duda replied "the next update can do this"
  (https://xferrecords.com/forums/general/serum-lfo-grid). Noise Harmony: "The X and Y grids
  are now independent, and odd gate patterns get much quicker to draw"
  (https://www.noiseharmony.com/post/17-advanced-tips-for-serum-2).
- **Snap** is a modifier (Alt) in Serum, not a toggle; there is also a grid-snap toggle in the
  wavetable-import flow ("Adjust Smoothing and Grid Snap",
  https://www.mind-flux.com/news-1/2025/11/10/using-wavetables-as-lfo-curves-in-serum-2-modulation-from-sound).
- **Step/brush tools**: Shift-drag is the step tool; the right-click menu sets the "segment
  shape" the step tool stamps. Serum 2's What's New says "LFO Drawing Tools — Use drawing tools
  with a dedicated editor" (p.13) but does not enumerate them.
- **Shape presets**: "This folder icon will display a pop-up menu … many presets included …
  the bottom option … 'Save LFO Shape..'" (manual p.35). The files are `.shp`, "the same file
  format as the shape files used in the LFOTool plug-in", and the SAME library shows up in
  three editors: "the LFO section … the waveshaper (X-Shaper) … The Remap editor" (manual
  p.67). Serum 2 keeps it: "Presets — Choose a preset or create your own" (What's New p.13).
  I could not confirm an ".XferShape" extension from any primary source; `.shp` is what the
  manual documents.
- **SMOOTH**: a knob on the LFO panel, not in the graph: "this smooths the LFO output. This is
  useful for avoiding abrupt jumps in the LFO output, without having to draw ramps on every
  segment" (manual p.38). Still present in Serum 2 (Mind Flux, above).
- **Copy from wavetable**: Serum 2 "Import from Wavetable" in the LFO menu converts a table
  into an LFO curve (Mind Flux, above); the reverse also exists — Alt-drag an LFO tile onto an
  oscillator to make it the wavetable (https://outerverse.fm/blogs/tutorials/10-serum-2-secrets).
- **Modulate a point**: Serum 2 lets you "right-click any point in an LFO and choose Modulate X
  or Modulate Y" (Outerverse, above).
- **Path mode** (new, 2-D): "you draw your path by dragging on the field" — an X/Y field, each
  axis its own source (https://www.mind-flux.com/news-1/2025/11/9/drawing-custom-lfo-paths-in-serum-2-turning-modulation-into-rhythm-design).
- **Is there a freehand pencil?** In the LFO graph, **no** — it is points + tension + Shift-step.
  The pencil lives in the **wavetable editor**'s Draw Tools (manual p.43: Flat Line, Slope
  Up/Down, Sine, Half sine, Curve up/down, Interpolate, nudge, noise; Serum 2 adds a Pencil,
  Line, Warp and Smooth tool per https://necolebitchie.com/how-to-draw-wave-forms-in-serum/).
  Path mode is the only "free drag" in the LFO.

### 1b. Modulation curves on a connection (the Matrix "Curve")

Yes — every route has one, and there are two layers:

1. **The quick Curve column** (Serum 1 and 2): drag the little curve icon on the row. "Curve
   bends the response so the modulation eases in gently or slams in fast … click and drag
   this up to get an exponential effect, or drag down to get a logarithmic effect"
   (https://monosounds.studio/serum-2-modulation-guide/; https://www.presetdrive.com/serum-modulation-matrix-deep-dive/).
2. **The full editable curve** (new in 2): Xfer's own list says "Source Curves — Editable
   source scale curves · Aux Source Curves — Editable aux source scale curves" (What's New
   p.14). Users describe the door: "Double click the modulation curve in the matrix and it
   opens an editor which functions as a remap with preset saving too" and "right-click on the
   mod slot curve and enable remapping mods. You get a nice editor and everything" (KVR,
   https://www.kvraudio.com/forum/viewtopic.php?t=619315). Noise Harmony: "Each modulation
   route now has a curve editor. Instead of just scaling modulation strength, you can bend it,
   shape it, or make it step like a sequencer." Splice: "fully editable modulation remap curves"
   (https://splice.com/blog/serum-2-advanced-features/). A whole video exists on it: "Serum 2
   Custom Curves / Mod Remaps Explained", https://www.youtube.com/watch?v=xtxP3pc4afE
   (transcript not fetchable from here).

What it is FOR: make a macro or wheel ramp exponential/log, invert, put a knee in velocity,
turn a smooth LFO into steps, convert a bipolar source into a unipolar response — all without
touching the source itself. The **Aux** curve does the same to the second, scaling source
("velocity as the auxiliary source … the LFO effect is stronger on harder notes", Preset Drive).

### 1c. The other curve editors, and size

- **Warp "Remap"**: "A magnifier glass will appear … which opens a graph to change how the
  waveform will remap. A diagonal line from lower-left to top-right would indicate no change
  (y=x). The WT Pos knob determines the strength of the remap, from 0 (y=x) to 100 %" (manual
  p.15). Remap 2 mirrors, Remap 3 is a fixed sine, Remap 4 tiles it 4×. Serum 2 adds warp modes
  (FM, PD, filters, distortions, Dual Warp — What's New p.6) but I found no documented "Draw"
  warp mode by that name.
- **X-Shaper (FX)**: "Edit A / Edit B … a pop-up editor … X representing input level, Y the
  remapped output level" (manual p.22) — the same `.shp` shapes load there.
- **Filter Pattern Editor / PZSVF**: "draw your own filter shapes on a graph — just like you
  would in an EQ … save multiple curves and assign them to macros" (Noise Harmony;
  https://musicproductionwiki.com/articles/how-to-use-serum-2).
- **Wavetable editor**: pencil/line/warp/smooth draw tools (1a).
- **Pop-out / enlarge**: I found **no** documented detach or per-editor enlarge for the LFO
  or curve editors. The whole Serum window scales (logo / corner drag), and the Remap and
  X-Shaper graphs open as "pop-up editors" inside the window. Treat "Serum pops out the LFO"
  as unconfirmed.

---

## Part 2 — What ours does (read from the code)

One editor, three hosts, all in `ui/public/index.html`. The host contract is documented at
`index.html:34339-34348`: a host owns `pts()/setPts()`, `push()`, `title()/foot()`,
`odd()/drawn()`; everything else is shared. `window.__crvOpen(spec, anchor)` is the door
(`:35155`); `window.__crvUtil` exports `fit / bake / isStraight / unity` (`:35191-35200`).

**Point model**
- Points are `[x, y, tension]`; the cap is 32 (`:34805` `if(p.length>=32)return;`, `:34727`
  `if(out.length>32)return false;`; fitter budget `:34497` `fitRun(arr,0,N-1,N,32)`).
- Double-click empty = add point; double-click a point = delete (`:34803-34808`); double-click
  a tension dot = straight (`:34801`).
- **Per-segment tension**: drag the midpoint dot (`.cdh`) up/down, with a zero detent
  (`:34827-34833`). Evaluator is exponential-bias, non-periodic (`:34415-34417`).
- Vertical steps are legal (two points on the same x) (`:34466-34470`).
- Alt-move-all-tensions and multi-select do NOT exist here (searched `:34795-34850`).

**Grid + snap**
- One ladder for both axes: `CGRIDS=[2,3,4,5,8,12,16,24,32]` (`:34297`), two sliders "Level"
  (`gh`) and "In" (`gv`) in the right-click menu (`:34853-34878`, `:35001-35002`). So X and Y ARE
  independent counts, but from the same 9-step ladder (no arbitrary "8×12"; 12 and 8 are both
  on the ladder, so that particular case works).
- Snap is a persistent toggle in the footer (`:34599`) and the menu (`:35003`); "Snap points to
  grid" quantises every point at once (`:35004-35013`).

**Brushes / tools** (`:35019-35024`): Point·edit, Straight line, Ramp up, Ramp down, Square,
Triangle, Sine, and **Freehand·draw it** (tool 7). The stamp geometry is `:34713-34724`.
Shift-drag = step draw with the current brush (`:34823`). Left-half is generated ink in odd mode
(`:34715`).

**Freehand pen** (fb560): a stroke fills a 129-cell buffer, then runs the SAME `fitDense`
Send-To-Shaper uses, so handles appear under the pen and it is audible while drawing; snap on =
one flat shelf per grid column (`:34745-34780`, `:34757`). Measured: 61-sample stroke → 14
tensioned points (memory note fb560).

**Seeds** (`:34880-34884`): Unity, Gentle S, Razor Stair, Fold Sine. Plus **Wavetable → Curve**
(Osc A-D, `:35029-35030`) and **LFO → Curve** (LFO 1-10, `:35031-35032`), Flip vertical /
horizontal (`:35014-35016`), Unpin centre (Distortion only), Clear to unity (`:35033-35035`).

**Per-point modulation**: right-click a point → "modulate Y" by LFO 1-10 with an amount rail
(`:34979-34993`, `:34951-34978`). Serialised only in the Distortion blob (`:34503`) and parsed in
C++ (`PluginProcessor.cpp:13567`, `:13671`).

**Odd symmetry**: `odd()` per host; on Distortion it is the Shaper family / PILL2 toggle
(`:34362`, `toggleSym` `:35074-35081`); mirrors the right half with exact tensions (`:34537`).
Guests return `false` (`:20101`, `:31594`).

**Send To Shaper / capture**: Distortion double-click any non-drawn mode → its REAL engine curve
→ ≤32 points with exact tensions (`:34384`, `:35040-35066`). On the warp host the FIRST drag
captures (mode flips to 37/38, amount lands at 1.0 — `:20115-20136`).

**Morph = depth** on the Distortion host: the A-D banks are a depth ladder, straight line → your
curve (memory fb559; `DistortionEngine.h bakeCurves`); measured −135 dB at 0, +1.5 dB at 1.

**The three hosts**
1. **Distortion transfer curve** (`DSTH`, `:34363-34411`): anchor pinned at centre (f(0)=0),
   Harmonics bars, live occupancy line, Beyond in the footer, pop-out allowed.
2. **Warp slot** (`:20095-20140`): title "<mode> · OSC A · WARP 1 — Curve"; footer drag =
   Amount; mode **37 Draw = phase map**, **38 Draw Amp = transfer curve**
   (`PluginProcessor.cpp:123-127`; `SynthVoice.h:1354` `DrawCurve pts[129]`).
3. **Mod connection** (`:31582-31609`): `key:'mod'`, unipolar, no anchor; title
   "<source> — Curve"; footer **straighten** sets `a.curve=null` (`:31598-31600`); `push()`
   bakes 129 samples and drops the curve entirely if it is straight (`:31601-31606`).
   On the wire: `kModCurvePts = 129` (`SynthModConfig.h:1060`), 32 curves per patch (`:1061`),
   applied by `applyModCurve` (`:1197-1211`) from both the per-voice path (`SynthVoice.h:3233`,
   `:5761`) and the global pass (`PluginProcessor.cpp:8527-8594`). X is the source normalised
   0..1 (envelopes/followers, velocity/macro/wheel/aftertouch 0..1, LFO −1..+1 mapped in and
   back — `:1195-1211`). Measured Key→Osc A Level: dry 217 · straight 347 · y=1−x 87 (fb559 note).
   **How you open it**: the Extend glyph on a route row in the control menu (`:31674`) or on
   the hover route list (`:31826-31829`, tooltip "Extend — open this connection's curve.
   Reshape what this modulator does on its way to the knob."); the glyph lights when a curve
   exists (`:31672`).

**Pop-out**: only the Distortion host. Both doors (⧉ and drag-past-the-edge) refuse a guest with
a toast "This curve lives in the patch — only the Distortion curve pops out"
(`:35298-35311`, `:35335-35337`). The hero is a fixed 456×220 (`:34523`).

**Smooth / slew**: none on any curve host. The only "Smooth" is the Distortion's post-filter
(`DistortionEngine.h:363`), unrelated.

**Gates**: `Tests/fb559_ui.js`, `Tests/dst_morph_depth.cpp`, `Tests/curve_probe.mm`,
`Tests/fb560_marks.js` exist, but `Tests/README.md` does not list fb559/fb560/curve_probe
(grep returned nothing) — the README is behind.

---

## Part 3 — For Max: what a connection curve is, and where we stand

### What it is
Every modulation route is "source × depth → knob". The curve sits between them: left-right is
how much the source is giving right now, up-down is how much the knob actually gets. Straight
diagonal = the route you already had. Anything else is you *reshaping the feel* of that one
route without touching the LFO, the envelope or the macro itself, so the same LFO can feel
different on two knobs.

### Three musical uses
1. **Mod wheel → filter, but it feels right.** A straight line makes the first quarter of the
   wheel do too much. Drag the midpoint tension down (log-to-exp) and the cutoff opens slowly at
   first and rushes at the top — how a real filter feels under a hand. (Serum: "drag up …
   exponential, drag down … logarithmic".)
2. **Velocity → level with a knee.** Flat-ish for soft playing, a shelf, then a steep rise: soft
   notes stay usable, hard notes bite. Two points and one tension. Gentle S seed is one click.
3. **Turn a sine LFO into a bounce or a gate.** Razor Stair seed or the Square brush on the
   connection makes any smooth LFO step on THIS knob only; Flip vertical inverts a route; a
   half-curve turns a bipolar LFO into a one-sided (unipolar) wobble.

### Where we already match or beat Serum 2
- Points + per-segment tension + double-click add/remove: match (`:34803-34833`).
- Independent X/Y grid: match (two sliders, `:35001-35002`), Serum only got this in v2.
- Snap as a *toggle* plus "snap all points": ours; Serum needs Alt held.
- Step draw with SEVEN brush shapes (`:35019-35024`): Serum's Shift-step has one selectable
  segment shape.
- **Freehand pen that turns into real points** (`:34757`): Serum's LFO graph has no pencil at
  all (it is only in the wavetable editor / Path mode).
- Capture ANY engine curve into points (Send To Shaper `:35040`, warp capture `:20115`):
  Serum has no equivalent for its warp modes or distortions.
- Wavetable → Curve and LFO → Curve (`:35029-35032`): match Serum 2's "Import from Wavetable".
- ONE editor, identical grammar in all three places (`:34339`): Serum's LFO, Remap and X-Shaper
  are three graphs that merely share a preset folder.
- Per-point "modulate Y by LFO" (`:34979`): matches Serum 2's Modulate X/Y (we lack Modulate X).

### The real gaps (ranked)
1. **SMOOTH / slew on the curve output.** Serum has a per-LFO Smooth knob so steppy shapes do
   not click; we have nothing on any host (only the distortion's post-filter). Most audible gap,
   and it is a one-pole per route/slot in C++ plus one footer rail.
2. **Shape presets library (save / load).** Serum's `.shp` folder shows in all three of its
   editors. We have four hard-coded seeds (`:34880`) and no "Save shape…". A user folder of JSON
   point lists, shown in the Seeds section, closes it.
3. **Editor size for the guests.** The mod and warp curves are stuck at 456×220 (`:34523`) and
   refuse to pop out (`:35308`). Either teach the popped window a guest boot path (it only knows
   `openCrv(null)`) or add an in-window "enlarge" that doubles the hero.
4. **Alt = move all tensions / multi-select drag.** Serum's alt-drag-any-curve-point and
   rubber-band select are absent (`:34795-34850`). Cheap, and producers know the gesture.
5. **Modulate X per point** (Serum 2 has X and Y; we have Y only, `:34979`). Also: per-point
   modulation on the warp and mod hosts is drawn but inert — `U.bake` uses `evalP`, which reads
   only `[x,y,tension]` (`:34415`, `:35198`), and only the Distortion blob serialises it (`:34503`).
6. **Arbitrary grid counts.** Ours is a 9-step ladder (`:34297`); Serum is any integer. Low
   priority — the ladder covers the musical divisions.
7. **Serum's quick "drag the row icon" curve** (exp/log without opening anything). We always
   open the editor. A drag on the route-row glyph that sets one tension is a nice shortcut.
8. **Tests/README.md** should list `fb559_ui.js`, `curve_probe.mm`, `dst_morph_depth.cpp`,
   `fb560_marks.js`, `fb560_wtarrow.js`.

### One-line recommendations
1. Add a Smooth rail to the curve footer (per host) backed by a one-pole slew in
   `applyModCurve` / the warp draw path.
2. "Save shape…" + a user shapes folder feeding the Seeds section on every host.
3. Let guest hosts pop out (send the host spec, not a 3-letter id) or add an enlarge toggle.
4. Alt-drag a tension dot = all tensions; drag on empty = rubber-band select.
5. Either wire per-point modulation through `bake` for guests or hide the menu there.

---

## Sources
- Xfer, *What's New in Serum 2* — https://static.xferrecords.com/Serum%202%20What's%20New.pdf (pp.13-14: LFO Drawing Tools, Presets, Independent Grid; Source Curves, Aux Source Curves)
- Xfer, *Serum Manual* (v1; gestures unchanged) — https://s3.amazonaws.com/decembercymatics/Serum_Manual.pdf (pp.15, 22, 35, 38, 43, 67)
- Xfer forum, "SERUM LFO GRID" (Steve Duda reply) — https://xferrecords.com/forums/general/serum-lfo-grid
- KVR, Official Serum 2 thread — https://www.kvraudio.com/forum/viewtopic.php?t=619315 ; page 47 — https://www.kvraudio.com/forum/viewtopic.php?t=619315&start=690
- Monosounds, Serum 2 Modulation guide — https://monosounds.studio/serum-2-modulation-guide/
- Noise Harmony, 17 Advanced Serum 2 Tips — https://www.noiseharmony.com/post/17-advanced-tips-for-serum-2
- Outerverse, 10 Serum 2 Secrets — https://outerverse.fm/blogs/tutorials/10-serum-2-secrets
- Splice, Serum 2 advanced features — https://splice.com/blog/serum-2-advanced-features/
- Mind Flux, wavetables as LFO curves — https://www.mind-flux.com/news-1/2025/11/10/using-wavetables-as-lfo-curves-in-serum-2-modulation-from-sound
- Mind Flux, Path LFO — https://www.mind-flux.com/news-1/2025/11/9/drawing-custom-lfo-paths-in-serum-2-turning-modulation-into-rhythm-design
- Production Music Live, custom LFO shapes — https://www.productionmusiclive.com/blogs/news/10-quick-serum-tips
- Preset Drive, Mod Matrix deep dive — https://www.presetdrive.com/serum-modulation-matrix-deep-dive/
- MusicProductionWiki, How to use Serum 2 — https://musicproductionwiki.com/articles/how-to-use-serum-2
- YouTube, "Serum 2 Custom Curves / Mod Remaps Explained" — https://www.youtube.com/watch?v=xtxP3pc4afE (not fetchable; title only)
- Necole Bitchie, drawing waveforms in Serum — https://necolebitchie.com/how-to-draw-wave-forms-in-serum/
- Memory notes: `~/.claude/projects/-Users-macshooter-Developer-VST-Plugins/memory/terrain-one-curve-editor-fb559.md`, `terrain-curve-menus-marks-arrow-fb560.md`
