# Organics — oscillator engine mockup (v1)

Sampled acoustic instruments for Terrain. The engine uses the same panel as every other oscillator engine:
the same header, the same 302.65 × 65 display box as Sample / Modal / Resynth, the same knob row and the same
thin page arrows. The one new thing is the picture: **a white 3-D line drawing of the instrument that reacts when you play.**

Nothing in `Source/` was changed. Everything lives in this folder.

| File | What it is |
|---|---|
| `organics-mockup.html` | A single self-contained page (no external assets). **A** is the panel in the synth page, next to a real render of the Modal panel. **B** is the same panel with all 23 instruments. **C** is the instrument browser. **D** is family coverage. Play it with **A–K** (black keys **W E T Y U**, octave **Z/X**) or the little keyboard. Click the name to open the browser, use ‹ › to step through instruments, drag a knob to change it, and use the thin arrow to flip to the Unison page. |
| `organics-instruments.js` | The line-art set, keyed by family id. It is a tiny 3-D kit plus one builder per family. `OrganicsArt.render(id)` returns the SVG group and animation anchors. `OrganicsArt.svgString(id)` returns a standalone SVG. It has no dependencies, and the engine can inline it as is. |
| `svg/*.svg` | All 23 drawings exported as static SVGs at the box's size (2× pixel size). |
| `shot-compare.png` | The real Modal panel (rendered headless from `index.html`) next to the Organics panel, at the plugin's 1.463 zoom and 2× DPR. |
| `shot-panel-*.png` | The panel with grand, violin, guitar, trumpet, choir, organ, marimba and flute, plus `shot-panel-cello-unison-page.png` (page 2). |
| `shot-reactions.png` | Six instruments captured mid-note. |
| `shot-gallery.png`, `shot-browser.png`, `shot-full-page.png` | The gallery, the open browser and the whole sheet. |

## House style: copied, not invented
- **Header.** `OSC • Organics` uses the real `.device-label-row` rules (9 px / 300 / 2 px tracking, white while the osc is on, a 2 px dot at 42 % white). The name reads `‹ Grand Piano ›` with the Sample engine's `.samp-nav` arrows and the Modal family recipe: `top: 6px` for the corner cluster and `-1.5px` on the name text. The `[A]` and `[+]` boxes are 16 px with a 0.7 px border. Verified against the real render: OSC, the name and the A/+ glyphs all sit on the same centreline as Modal's (`shot-compare.png`).
- **No category pill in the header.** It collided with `‹` for longer names. The shipping Sample engine has the same collision ("One-Shot‹" in the reference render). So the header works like Wavetable's: click the name to open the browser.
- **Display box.** `.samp-disp`, 302.65 × 65, radius 8, colour `#10101F`. That is the measured 16,16,31 of the real box (`#1A1A2E` under `rgba(8,8,18,.52)`).
- **Knobs.** The synth's own `.kr-svg`: a 270° track at 13 % white, the value arc at 92 %, and the value inside the ring (7.5 px). Labels are 8 px / 300 / 1.6 px tracking. A bipolar knob gets the centre notch. Grid is 5 columns, 4 px gap.
- **Browser.** This is the mini browser's `.pmenu.tp-q` glass, unchanged: the blur, the border, the `•` current row, and the purple `.ps` headers. **Categories are in the left column in the mini browser's category purple (#B794FF).** Instruments are on the right, each with a tiny line-art thumbnail. The footer reads "Browse all".

## The knob set
Page 1 has the five Organics knobs. The thin arrow flips to page 2, which is **Terrain's existing Unison row** (Voices · Stack · Range · Detune · Blend · Width), as Max described.

| Knob | What it does |
|---|---|
| **Tone** | Brightness that follows velocity the way real instruments do: a key-tracked, velocity-scaled tilt/low-pass. At 0 it is dark and felted; at 100 it is open and bright, and hard hits get brighter still. |
| **Body** *(bipolar)* | Moves the resonances or formants without changing pitch. Below 0 sounds like a smaller instrument (violin toward a toy); above 0 sounds like a bigger one (violin toward viola or cello). |
| **Dynamics** | Crossfades between the sampled dynamic layers (pp ↔ ff timbre) independently of loudness. This is the mod-wheel "expression" of orchestral libraries, and the most "alive" control a sampled instrument has. |
| **Release** | Level and length of the release samples and key-off noise: the damper thud, bow lift, breath end, key clack. At 0 notes end dry; at 100 you hear the room and the mechanism. |
| **Humanize** | Round-robin depth plus micro variation per note: a few cents of detune, a few ms of timing, and small tone and level drift. At 0 every note is the same sample; at 100 it sounds like a player, not a machine. |

**Change from the brief: Dynamics replaces Attack.** Terrain already has an amp envelope with an Attack (`SYN_ENV_AMP_A`), so an Organics Attack knob would mostly repeat it. Dynamics (the layer crossfade) can't be done anywhere else in the synth, and it matters most for making samples feel played. If Max wants onset softening too, it can be added to Tone's low end (a soft onset below ~20 %) or given a slot on a future third page. Candidates for that page are Attack, Noise/Air, Width, Velocity sens and Tune.

## The visualizer
**How the drawings are made.** Each instrument is a small 3-D model in centimetres at real proportions: extruded outlines, swept tubes, spheres and decals. Every model goes through **one shared orthographic three-quarter camera** and is emitted as flat SVG. That is why the whole set looks like one family: same eye, same line weights, same stroke width at any size. The camera yaw/pitch is nudged per family, from −10° to −66° yaw and 12° to 40° pitch, so each instrument shows its most recognisable face.

- **Line weights (in box units, so the plugin zoom scales them):** main 0.8 at 84 % white, hairline 0.55 at 40 %, faint 0.5 at 18 %. Rendered at the plugin's 1.463 zoom these come out at about 1.17 px, 0.8 px and 0.73 px. There is no glow anywhere.
- **Hidden lines.** Every part is filled with the box colour and parts are painted back to front, so nearer parts cover what is behind them. Inside a part, back-facing walls and the far half of tube ends are never emitted. Tubes use the exact orthographic silhouette of a swept surface of revolution, which is why the trumpet, horn and clarinet bells flare correctly.
- **Fit.** Each drawing is scaled to fill the box's height, or its width for long instruments such as the flute, koto and marimba. Pianos, Rhodes and the tonewheel organ are framed on the case, and their legs fade into the bottom of the box through a static mask gradient. The instrument therefore fills the box instead of shrinking to fit its legs.

**How each family reacts.** There is no audio in the mockup. Every reaction is either a CSS class flip (a flat tint plus a stroke going to full white) or a pooled path whose `d` is rewritten.

| Family | Reaction |
|---|---|
| Keys (grand, upright, harpsichord, Rhodes, clavinet, tonewheel) | The played key dips 0.45 px, its outline goes white and its top gets a flat tint. Grand, harpsichord and clavinet also ring **their string inside the case**. |
| Bowed (violin, cello, bass) | The string for the note (the highest open string at or below it) becomes a thin vibrating "lens": two quadratic curves that breathe while the key is held and settle after release. |
| Plucked (guitar, harp, koto) | The same lens, struck and then decaying: τ 0.9 s, or 1.4 s for the harp. |
| Tines (kalimba, music box) | A cantilever lens that only swings at the free tip. |
| Mallets (glockenspiel, marimba) | The bar tints and dips for 70 ms, then fades over 1.6 s (glockenspiel) or 0.7 s (marimba). |
| Winds (flute, clarinet, sax) | Key cups close according to pitch (lower notes close more). Thin arcs leave the bell or embouchure every 430 ms while the note is held. |
| Brass (trumpet, horn) | Valves press using the real trumpet fingering mod 12, and arcs leave the bell. |
| Voices (choir) | Two singers per note open their mouths (a small ellipse scales up). |
| Organ (pipe organ) | The pipe for the note lights while held. |

## CPU (measured)
Headless Chrome, 1260 × 900 at 2× DPR, with only the playable panel on screen. The data comes from CDP `Performance.getMetrics` deltas and a rAF frame counter. The scripted playing is a note every 140 ms, each held 280 ms, so 2–3 voices overlap.

| State | Main-thread ms / frame | Our reaction JS ms / frame | Frame p50/p95 |
|---|---|---|---|
| Idle (nothing playing) | 0.06 (this is the measuring rAF loop itself; Organics runs **no** loop when idle) | 0 | 16.7 / 16.8 |
| Grand (keys + interior strings) | 0.37 | 0.027 | 16.7 / 16.7 |
| Violin (bowed lens) | 0.24 | 0.022 | 16.7 / 16.7 |
| Guitar | 0.18 | 0.010 | 16.7 / 16.7 |
| Trumpet (valves + WAAPI arcs) | 0.30 | 0.002 | 16.7 / 16.8 |
| Choir | 0.33 | 0 (pure CSS) | 16.7 / 16.8 |
| Pipe organ | 0.18 | 0 | 16.7 / 16.7 |
| Marimba | 0.32 | 0 | 16.7 / 16.7 |
| Kalimba | 0.30 | 0.026 | 16.7 / 16.8 |

That works out to **≤ 0.37 ms of main-thread work per frame while playing (≈ 2 % of a 60 fps frame)** and ≈ 0 when idle. The only per-frame JS is the string lens, which rewrites one `d` attribute per ringing string, and the loop stops as soon as nothing is ringing. Building a drawing costs about **1.1 ms** on average per instrument switch: 25.8 ms for all 23 in Node. SVGs are 9–73 KB with 21–338 paths. Raster happens on the compositor and is not counted above; it is a static, mostly unchanging SVG. For comparison, Terrain's own UI budget is its `__uiPaceFps` pacing, and this sits well inside it.

**Integration notes.** Build the drawing once when the instrument changes and cache it; `render()` is pure. Drive `noteOn` / `noteOff` from the same lane that already sets `__notesActive`. Hook the tick into `__tiFrameReg` instead of a private rAF so it parks with the rest of the UI. Set `--org-bg` to the box colour (fills are what hide lines, so they must match).

## Families
**Drawn (23):** Grand Piano, Upright Piano, Harpsichord, Electric Piano (Rhodes Stage), Clavinet, Tonewheel Organ, Pipe Organ, Violin, Cello, Double Bass, Acoustic Guitar, Harp, Koto, Flute, Clarinet, Alto Sax, Trumpet, French Horn, Choir, Glockenspiel, Marimba, Kalimba, Music Box.

**Still to draw for a full library.** Most can reuse an existing builder, noted in brackets:
- Keys: Wurlitzer (Rhodes case, different lid), Celesta (upright case), Accordion.
- Strings: Viola (`bowed()`), String Section (three bowed instruments staggered), Electric Bass, Nylon Guitar (`guitar` with a slotted head), Mandolin, Banjo, Sitar (long neck plus gourd; `tube` + `sphere`).
- Winds: Oboe and Bassoon (`clarinet` with a thinner bore), Recorder, Pan Flute.
- Brass: Trombone (the trumpet bell plus a slide), Tuba (the horn's coil plus an upright bell), Brass Section.
- Voices: Solo Voice (a single bust, or a studio microphone).
- Mallets and bells: Vibraphone (marimba plus a motor shaft and fans), Xylophone (marimba without resonators), Tubular Bells (tube rack), Handbells, Steel Drum, Timpani.

## Honest assessment: weakest drawings
1. **Harp.** It reads as a harp, but a harp is ~0.6 : 1 and the box is 4.7 : 1, so it fills only about a sixth of the width even at full height. Same for the **Music Box** and **Upright Piano**, which are right but small. Options are a lap/lever harp at a wider angle, or accepting the empty space.
2. **Alto Sax.** Lying on its side it is recognisable, but the upturned neck and the large bell mouth read slightly like an ear-trumpet. The best fix is a proper crook angle on the neck and pad cups on the true front face.
3. **Tonewheel Organ.** It is a cabinet with two manuals and drawbars, but at this size it could be any console. It needs the B-3's rounded "ears" and a Leslie beside it to be unmistakable.
4. **Cello vs Double Bass.** Both are correct, but at thumbnail size they differ mainly in the name. The bass needs its sloped shoulders and machine-head tuners.
5. **Rhodes and Clavinet black keys** are outlined boxes, so at this scale they read white. Filling them with a light tint may read better; this is a matter of taste.

The strongest drawings are the trumpet, grand piano, harpsichord, violin, guitar, flute, clarinet, choir, pipe organ, glockenspiel, marimba, kalimba and French horn.
