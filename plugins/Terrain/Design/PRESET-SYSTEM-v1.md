# Terrain — Preset System v1 (build contract)

Mockup: `Design/preset-system-v1-mockup.html` — interactive + audible. Open it, click things.
Ground truth it is built on: memory `terrain-preset-system-ground-truth` + `terrain-preset-and-environment-vision`.
This page is the decisions, not the research. Sign the mockup off; then this is what gets built.

## 1. Two surfaces, one system
| surface | opens from | is |
|---|---|---|
| **Quick menu** | click the preset name in the header | the `.pmenu` glass, grown to two columns: BANKS · that bank's presets grouped by TYPE. `‹ ›` step inside the current bank. Favourites is a pseudo-bank. "Browse all" at the foot. |
| **Browser** | Browse all, or the dice | **the `.pmenu` glass, full-window**, covering 44→656 **above `#syn-panel`** (today's drawer is z 15 under a z 30 page — it renders behind the synth). Search · Type chips · Style chips · Banks rail · Author rail · sortable list · inspector. Dice + heart sit on the right by the count. |

Load rules: single click = select + audition, double-click / `↵` = load and close, `↑↓` step, `esc` closes, space = favourite, dice = random inside the current filter (**Max's intent for the dice is to GENERATE a new preset — deferred; for v1 it picks**). Column heads Name · Carries · Type · Style sort; click again flips, a third click clears. Right-click a User preset: Load · Favourite · Rename · Overwrite with current · Delete (rose). Factory: Copy to User.

**No caps, no boxes, one font.** Nothing new is uppercase. Env / User / Environment are plain purple or grey letters on the baseline — never a badge. The header's + ‹ › and gear have no boxes. One font family, hierarchy by colour (`--text-soft` for labels).

**One selection grammar, no fills — copied from the four mode tiles (`#syn-panel .flow-mode`, fb118/fb119).** Resting: an outline (`rgba(255,255,255,.45)` dark / `rgba(42,32,53,.30)` light), transparent inside, secondary text. Hover: text goes primary, nothing else moves. Selected: `border-color: var(--purple-400); color: #fff; background: transparent`, glyphs get `drop-shadow(0 0 4px rgba(255,255,255,.5))`. Applies to BUTTONS: the header pills (SYN MIX DLY EQ MOD included), chips, toggles, the heart, Load/Save/Import/Export. Selection MENUS — the banks and author rails, the list rows, the quick-menu lines — are not buttons and keep the purple wash highlight (Max, rev 5). Icon buttons with no box at rest (dice, heart, + ‹ ›, gear) show the outline only on hover/selected. The shipping `.pmenu` hover/current wash and the filled `.mod-btn.active` change to this when the browser is built.

**Furniture.** One + glyph (the 24-box, 2.2-stroke plus) and one ✕ (`__xGlyph`) everywhere. The browser's column header is transparent like everything else, with its separator — and it sits ABOVE the scroll region, not sticky inside it, so no row can ever pass under it. Dice and heart sit flush to the ✕ (4 px gaps); the ✕'s right edge is the gear's right edge.

**Notes.** Every preset has a free-text note (inspector, click to edit, saved with the preset, read by search). Analog Lab's comment field: "turn the mod wheel up, it's connected to Macro 3". Inspector blocks never shrink; notes absorb the leftover height and scroll inside, so Load never moves. Chip strips scroll sideways with the + inline at the end — as many types and styles as anyone adds.

## 2. Naming, taxonomy
- A preset's display name is **`Bank - Name`**, always. The dash is the bank. `Terra - Glacier`, `User - Slatt`, `Lowland - Heron`. Same law as the wavetables.
- **Terra** is the factory bank (100 presets, 10 per type). **User** is the one you save into. Imported packs are their own banks, named by the pack.
- TYPE (one per preset): Bass · Lead · Pad · Keys · Pluck · Texture · Perc · FX · Arp. **No Sequence — there is no sequencer yet.** No icons on types: a type the user adds has none, so none of them get one.
- STYLE (any number, first one shows in the row): Dark · Bright · Warm · Metallic · Glassy · Dirty · Clean · Evolving · Punchy · Wide · Hollow · Organic.
- **Both vocabularies are EDITABLE** (Max, rev 2–3): `+` at the end of the chip row adds a word; right-click a chip → Rename (re-files every preset that used it) or Remove (refused while presets still use it, with the count). Typing a new word in the save sheet adds it too.
- AUTHOR is free text, typed at save, filterable. BANK is typed at save: an existing name joins that bank, a new name creates one (Analog Lab's model). Factory banks refuse saves.
- **New bank…** in the rail creates an empty user bank. Right-click any non-factory bank → Rename · **Export bank…** (a manifest of exactly what goes into the `.terrainpack`: presets, wavetables, one-shots, IRs, flow cards, LFO shapes, environments + nodes, total size) · Remove.
- Search reads name · bank · type · styles · author · **effects** (the FX chain the preset uses); every word typed must match ("reverb dark"). Effects show in the inspector, capped at 8 then `…`.

## 3. CARRIES — what a preset embeds (the Terrain-only part)
Six slots on every row and in the inspector: **wavetables · one-shots · impulse responses · flow cards · LFO shapes · nodes**.
- A preset **embeds** its imported audio (FLAC-24 → base64, the fb602 decision) so it is portable Mac↔Windows and machine-to-machine.
- **Factory wavetables and factory one-shots are referenced by name, never embedded** — `Terra - Bit Ladder` resolves against the bundle on any machine. Only imported audio is carried.
- A preset with **nodes > 0 is an ENVIRONMENT**: the same file plus a patcher graph. The badge, the purple node mark and the loading wash all key off that one number. (Patcher graph schema is reserved, not designed here — the seat exists so the format never has to change.)
- The inspector prices it: a plain Terra preset ≈ 17 KB; one embedded one-shot ≈ 600 KB; an IR ≈ 1.2 MB. Users see the cost before they click.
- The inspector shows a **Load** row — `Instant` · `2–3 s` · `5–6 s` — never a sentence. Estimate from bytes + node count; the real build measures its own first load and refines it. The loading card shows the full `Bank - Name` in one colour.
- Carries marks, in order: sine (wavetable) · single note (one-shot) · decaying bars (IR) · card (flow card) · climbing notes — the old Arp icon (LFO shape) · three joined circles (nodes). Chosen so no two read as "another sine".

## 4. Format
- **`.terrain`** = the existing state chunk (`"VC2!"` + LE u32 + XML — so host sessions and presets are the same bytes) with one added root child `<preset>` carrying name · bank · author · type · styles · carries · format-version. Assets ride as root properties exactly as `wtImportPcm0..3` already does, re-encoded FLAC-24. **No new serialiser** — `setStateInformation`'s load order (migrations → JSON blobs → V1/V2 branch → `replaceState`) is load-bearing and is reused verbatim.
- **`.terrainpack`** = a zip: `pack.json` (name, author, version, counts) · `presets/*.terrain` · `assets/` (wavetables, one-shots, IRs as `.flac`; cards and LFO shapes as `.json`). Presets inside a pack reference pack assets **by content hash** so a bank of 100 presets sharing 3 wavetables stores them once. Import = unzip to the user root + register the bank; nothing is loaded until a preset is clicked.
- Both extensions are already claimed by `isInterestedInFileDrag` (PluginEditor.cpp:13228) and stubbed at `loadPatch` / `importTerrainPack`. Fill those seats.
- Sub-presets (a flow card, an LFO shape, a convolution IR) are the same asset files a pack carries; `TIC.presets` keeps its per-card menu and gains "add to pack".

## 5. DSP 100 % correct — the gate, not a promise
A preset is correct when **save → load is a null**. Two bars, both required:
1. **State round-trip**: every APVTS parameter (4072 at HEAD) and every root property (`synModJson`, `dynEnvJson_`, `lfoShapesJson_`, `dstCurvesJson_`, `warpDraw0..7`, `midiCcMap`, `macroNames`, `arpLanesJson_`, `noiseSampleSel`, `wtImportPcm0..3`, `cardStates_`, IR audio) byte-identical after a reload into a fresh processor.
2. **Audio null**: render a fixed note for 2 s before save and after load into a fresh instance — difference < −100 dBFS. Catches anything derived, cached, or rebuilt at load that the param dump would miss.
Both run headless against the shipped binary, with a mutant that drops one root property and must go red.

## 6. Load path and CPU
- **Normal preset**: parse on the message thread (15 KB gzipped, sub-ms), decode any embedded audio on the fb611 worker, hand the finished buffers back, one atomic swap under the existing lock. No overlay. Nothing on the audio thread but the swap.
- **Environment**: staged — parse → decode assets → build graph → swap. Progress is bytes-weighted across the stages, so the bar is honest. Overlay appears **after 120 ms** (a fast one never flashes it) and stays **≥ 400 ms** once shown (a medium one never strobes it). Wash is a flat rgba — **no backdrop-filter** on a full-window surface (fb613: the renderer pays for it, the CPU meter never shows it).
- **Browser**: rows built once per catalogue change, filtered by class, sorted by flex `order` — measured in the mockup: 3,890 DOM nodes at 105 presets, **0.92 ms per keystroke**, zero animation loops. The catalogue is one JSON index per bank, read once at scan, never re-parsed per keystroke. Virtualise the list only above ~600 rows.
- **The browser is glass** (`backdrop-filter: blur(20px) saturate(1.4)`, the `.pmenu` coat) over the synth page. A full-window blur re-composites every frame the page beneath repaints, so **while the browser is open the page beneath is frozen** — force `uiQuiet`, pause the rAF loops via the existing `__tiOff` gate — and the blur is composited once. The loading wash and the sheets stay flat (no blur).
- The browser never touches the audio thread; audition is a real load, same path as click-to-load.

## 7. Recycle map (no new code for existing things)
`.pmenu` → quick menu and context menu · `.cat-btn` → chips · `.user-badge` grammar → ENV/USER badges · `__extGlyph` SVG grammar → every icon · the dashed "Add Effects" box → the import drop zone · `savePreset / getPresets / deletePreset` natives → unchanged couriers · `#preset-browser` → replaced by the new surface at a z-index above 30 · AU program list follows the bank automatically.

## 8. Open with Max
- Author on factory presets: all "Waves Crate", or some "Max"? (Mockup shows both.)
- Node cap per type for environments (50?) — it changes nothing here, only the worst-case bar.
- Should a loaded environment show its node count in the header, or only in the inspector?
