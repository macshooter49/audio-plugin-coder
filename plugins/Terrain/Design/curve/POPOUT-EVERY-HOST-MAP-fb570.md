# Curve-card pop-out for guest hosts (WARP · MOD) — the map

Paths below: `index.html` = `Source/ui/public/index.html`; `PE` = `Source/PluginEditor.cpp`;
`PP` = `Source/PluginProcessor.cpp`; `PP.h` = `Source/PluginProcessor.h`; `Tests/` = `plugins/TerrainInstrument/Tests/`.
Every number is a line read in the current tree (fb569-era, index.html 37,813 lines).

---

## 0 · The one-paragraph answer

Today the curve card pops out through the generic fb82 card door, which carries **nothing but a
3-letter id** (`popOutCard('crv', x,y,w,h)` index.html:35340 → `popOutCardWindow` PE:5496 → `goToURL(root+"?card=crv")` PE:5365),
and the popped page's only boot is `__openCrvCard()` → `openCrv(null)` → the **Distortion host** refilled
from `getDistortionCurves` (index.html:35392-35395, 35147, 35144). fb560 therefore refuses guests at both
doors with a toast (35335-35337, 35309-35311). To let WARP and MOD pop out you need (1) a way to carry
the host's *identity* to the new window, (2) two main-only natives copied onto the card window's own list
(`getWarpCurve` PE:3294, `setWarpDrawCurve` PE:3278) plus a cardinality source (the card window has no
relays, so `__paramCardinality` index.html:19648-19658 returns 0 there and a warp capture silently refuses at
20117), (3) a mirror-merge fix in the mod module (restore merges DEPTH only for existing routes, 32208 —
a curve edited in the popped window never reaches the main view's `assigns`), and (4) a retarget path so
opening a different curve while popped switches the floating window instead of opening a second docked
card (the guard at 35123 only covers `!host`). **Recommend design B** (spec on the processor, new card
native `getCardBoot`) — it is the house pattern (fb83 `getPoppedCards`, fb137 `cardStates_`, fb232 blob),
it survives editor close/wd9 reload, and it is the only one that gives a clean retarget + Dock.
`updateModConfig`/`getModState` are **not** involved: they are the 3-LFO FX palette (PE:1912-1928; PE:5096-5098 says so);
the synth mod matrix the MOD host writes is `setSynthMod`/`getSynthMod`, already on the card list (PE:5216-5225).

---

## 1 · The three hosts

The editor module is one IIFE starting at index.html:34293 (`fb328 — THE DISTORTION CURVE CARD`).
`HOST` (34335) is null for the Distortion; `curHost()` returns `HOST||DSTH` (34342); `cPts/setPts/isOdd`
dispatch through it (34343-34345). `cPush()` (34509) stamps `__crvTouchT` and calls `curHost().push()`
40 ms later — every gesture in the editor ends in the host's `push`.

### 1a · DST host (`DSTH`, index.html:34347-34411)
| field | line | what |
|---|---|---|
| `key:'dst'` | 34347 | |
| `pts()/setPts()` | 34353-34354 | `CV.a` — the module's own bank array |
| `push()` | 34356 → `pushNow()` 34507-34508 | `blob()` (34503) → native **`setDistortionCurves`** (card list PE:5048-5053; main list body not needed — the card list is what the popped window uses) |
| `anchor:true, bars:true, live:true` | 34357-34359 | centre pin, Harmonics bars, occupancy feed |
| `capture()` | 34370 → `sendToShaper` 35041 | double-click Send To Shaper (DST only) |
| `title()` | 34372 | `TYPES[typeIdx()]+' — Curve'` |
| `foot()/footIdle()/menuHead()` | 34380-34411 | Beyond chip, Table source pills |
| boot pull | 34677-34682 | `getDistortionCurves` + `getDstTableSrc` on page load, retries 40×250 ms |
| `__crvPull` | 34688-34693 | preset-apply refresh |
| `PS(id,def)` | 34306-34307 | slider access: `__synSliderShim` when `__cardOnly`, relay `getSliderState` otherwise |

Identity: none — there is one distortion curve per instance (the processor blob, PP.h:768).

### 1b · WARP host (index.html:20055-20191, inside the warp-extension IIFE; door `__openWarpExt` 20200-20222)
Constructed inside `openWarpCurve(osc, slot, d, ev)` (20055). Everything that identifies it is a **closure**:
`pMode/pAmt/pVar` param ids derived from `osc`+`slot` (20057-20059), `mode`, `dom`, `amt`, `vr` (20060-20062),
`raw`/`pts` fitted from `d.pts` (20064-20074), `captured` (20074), `openedAs` (20086 — the fb560 title law).

| field | line | native(s) |
|---|---|---|
| `key:'warp', anchor:false, bars:false, live:false` | 20093 | |
| `pts()/setPts()` | 20094-20095 | closure `pts` |
| `title()` | 20102 | `openedAs · OSC x · WARP n — Curve` |
| `foot()` amount chip / `footDrag` | 20103-20111 | `__setSynParam(pAmt)` (13079-13085 → native `setSynParam`, **on card list** PE:4993) |
| `push()` | 20112-20148 | capture: `writeMode(37|38)` 20087-20091 needs `__paramCardinality(pMode)` (19648-19659 — reads `Juce.getSliderState(name).properties`, i.e. a **relay**) then `__setSynParam`; then `U.bake` → native **`setWarpDrawCurve(osc,slot,csv,rate)`** 20147-20148 — **main list only** PE:3278-3293 |
| follow-poll | 20169-20191 | native **`getWarpCurve(osc,slot)`** resolved at 20171 — **main list only** PE:3294-3302; 300 ms; one-owner token `wPollT` (20054, 20172, 20191); retires when `__crvOpenState()` (35189) says `!open` or `key!=='warp'` (20175-20176); sleeps 600 ms after `__crvTouchT` (20178, 20183); stands down once `captured` (20177) |
| open | 20149 | `window.__crvOpen(host, ev.target)` |

The door (20200-20222) fetches `getWarpCurve` first and routes filter modes to the fb546 card (20214), toasts on failure (20215-20218), else `openWarpCurve` (20219).

**Identity for a spec:** `{key:'warp', osc:'a'..'d', slot:0|1}` — the door re-derives everything else from C++, so the spec never needs points.

### 1c · MOD host (index.html:31582-31606, inside the synth-mod module; hooks 31611-31612)
`openCurveEditor(a, anchor)` builds the host around one entry `a` of `assigns` (31174 — `{lfo|env|…, dest, depth, el, curve?, byp?, aux?}`).

| field | line | native(s) |
|---|---|---|
| `key:'mod', anchor:false, bars:false, live:false` | 31586-31588 | |
| `pts()/setPts()` | 31591-31592 | closure `pts`, fitted from `a.curve` on open (31585) |
| `title()` | 31597 | `srcName(a)+' — Curve'` (31554) |
| `foot()` straighten / `footClick` | 31598-31600 | sets `a.curve=null`, `pushMod()` |
| `push()` | 31601-31605 | `a.curve = isStraight ? null : U.bake(pts,129)`; `pushMod()` = module `push()` 31094-31105 → native **`setSynthMod(json)`** (main PE:704, **card PE:5221**); guarded by `restoredOnce` in card windows (31098) |
| open | 31606 | `window.__crvOpen(host, anchor||document.body)` |

Entry points: the route-row ⤢ (31674 — the control-menu row; 31829 — the hover route list) and the probe door `__tiCurveEdit(i)` (31611, by index into `assigns`). Truth on the wire is `o.c` in the matrix JSON (31100-31101).

**Identity for a spec:** the route key. The module's own key is `srcKey(a)+'_'+a.dest` (31200, 31206) and the wire codec is `encodeSrc(a)` / `decodeSrc(code)` (31185-31186); `findRoute(S,dest)` (31205) resolves it. So `{key:'mod', s:<encodeSrc code>, d:<dest>}`.

Follow: the mod module's `restore()` (32196-32217) polls `getSynthMod` every 2.5 s (32220; card native PE:5216) and in **card windows** is a strict mirror (prune 32213-32215). `__tiModRestore()` (32219) forces one.

### 1d · Which natives exist where (PE)
| native | main list | card window list |
|---|---|---|
| setSynParam / getSynParam | 823 / 837 | **4993 / 5004** |
| setDistortionCurves / getDistortionCurves / getDistortionCurveViz | — / 1143 / 1159 | **5048 / 5054 / 5070** |
| getSynthMod / setSynthMod | 711 / 704 | **5216 / 5221** |
| getModState / updateModConfig (FX 3-LFO palette, wrong system) | 1924 / 1912 | 5099 / — |
| **getWarpCurve** | 3294 | **absent** |
| **setWarpDrawCurve** | 3278 | **absent** |
| cardinality (`__paramCardinality` needs a relay) | relays | **none** — 19648-19658 returns 0 without `getSliderState` properties |
| popOutCard / dragPoppedCard / getPoppedCards | 1195 / 1220 / 1232 | — |
| closeCardWindow / dockCardWindow / dragCardWindow / resizeCardWindow | — | 5296 / 5305 / 5252 / 5281 |
| setCardState / getCardState (fb137 per-card JSON on the processor) | 1029 / 1036 | 5109 / 5115 (store PP.h:783-789) |

A call to a native missing from a list is "dropped silently AFTER the IPC dispatch with NO completion" (PE:5075-5080) — the promise never settles. In a popped warp host today that would mean: `gwc` is non-null (20171), the 300 ms poll keeps calling it forever, every call leaks a promise (the fb342 class, ~7,200/min), and `push()` silently loses the curve at 20147.

---

## 2 · The pop-out flow today (crv)

### 2a · The two doors in the main view
- The shared card shell (`TIC.shell`, index.html:32603-32611) renders the header with `.pop` "Pop Out" (32607) and `.x`. The curve card is built through it in `buildCrv` (35069-35071, `id:'crv', cls:'crv-ext'`) and appended to `#syn-panel` when docked (35075, the fb231 stacking law).
- **⧉ click** — document-capture listener 35329-35343: finds the card by `CARDS` class (35286, 35333), **refuses a guest** when `__crvHostKey()!=='dst'` and toasts (35335-35337), else `popOutCard(id, r.left, r.top, r.width, r.height)` (35338-35340), marks `__poppedCards[id]=1` (35341) and hides the docked copy (35342).
- **Drag past the edge** — the shell's header `mousedown` (32639-32650) calls `__cardEdgeHandoff(cfg.id, card, ev, dx, dy)` on every move (32644). `__cardEdgeHandoff` (35298-35325): guest refusal + toast (35309-35311), 28 px threshold (35312-35313), `popOutCard` with 9 args (35316-35317), then keeps steering via `dragPoppedCard` until mouseup (35319-35323).
- `__poppedCards` lives only in the main view (35289) and is re-seeded from `getPoppedCards` on every fresh page (35327-35328 ↔ PE:1232-1240).

### 2b · C++
- `popOutCard` native (PE:1195-1219) → `TerrainUiCore::popOutCardWindow(id, rect, grab?, mouse?)` (PE:5496; decl PluginEditor.h:917).
- Whitelist PE:5503-5504 (`crv` present). Already-popped → rescue/refocus (5506-5522; the JS w=0 ping lands here). Placement 5528-5540 (`webView->localPointToGlobal`). Snapshot `getResource("index.html")` 5541-5542. `adoptCardWindow` 5543 → PP:521-527 (viz census).
- `TerrainCardWindow` (PE:4959): ctor 4963-5367 — its **own** native list 4993-5311, resource provider that ignores the URL and returns the snapshot (5312-5315), `setAlwaysOnTop` 5320, `addToDesktop()` 5329 (fb89), `toFront(false)` 5332 (fb83), CanJoinAllSpaces|FullScreenAuxiliary 5336-5350 (fb84), **`goToURL(root + "?card=" + cardId)` 5365**, 1 Hz truth-teller 5366/5424. `windowIgnoresKeyPresses` 5397-5400 (fb83). `dragMoveTo` 5464.
- Title is `"Terrain CRV Card"` (4967) — fine for guests too.

### 2c · The page's card-only boot
- `<head>` sniff 455: `__cardOnly = (location.search.match(/[?&]card=(arp|chop|gli|rbn|mod|lfo|crv)/)||[])[1]` — the **second** card-id whitelist (its own comment). `page=` sniff 456. `html.card-only` class 498; CSS 3464-3473 (everything `visibility:hidden` until `.ti-popcard`; `left/top:0 !important` 3471).
- `boot()` 35386-35438: `crv` branch 35392-35395 waits for `__openCrvCard`, `setActivePanel('syn')`, calls `__openCrvCard()`; pins `.ti-popcard` 35401; relabels `.pop` → "Dock" 35402; freezes every other body child (35407-35410 — `#syn-ctx-menu`'s branch becomes `.ti-ghost` so the card's right-click menu still lays out); `card-only-late` 35411; `resizeCardWindow` push 35413-35417; header drag → `dragCardWindow` 35420-35430; ✕ → `closeCardWindow`, Dock → `dockCardWindow` 35432-35437.
- `__openCrvCard` = `openCrv(null)` (35147). `openCrv(host, anchorEl)` 35118-35146: refocus guard **only when `!host`** (35123-35125), `switching` 35126, `HOST=host||null` 35127, placement from `anchorEl` or `#syn-panel .fxr-core[data-core="saturate"]` with a `{left:300,top:300,right:560}` fallback (35129-35133 — overridden by the `!important` pin in card-only), `paint()` from cache 35143, then `pullBlob` for DST / `HOST.pull(cb)` for a host that defines one (35144-35145 — neither shipped guest does).

### 2d · Dock and ✕
Card page → `dockCardWindow`/`closeCardWindow` (PE:5305/5296) → `callAsync` → `proc.dockCardWindow/closeCardWindow` (PP:538-545 / 529-536; works with the editor closed via the parked core, PP:543) → `TerrainUiCore::notifyCardWindowGone(id, redock)` (PE:5554-5562) evaluates **in the main view**:
`__cardWinGone('crv')` (index.html:34271 — deletes `__poppedCards.crv`) and, for Dock, `__redockCard('crv')` (34270) → `__openCrvCard()` → `openCrv(null)` → **the DST host, always**. Nothing remembers the guest: after a (hypothetical) guest pop the main view's `HOST` still holds the guest object (only `.open` was removed at 35342) but `__redockCard` never looks at it.

### 2e · What `__poppedCards.crv` does to a later in-plugin open
- Opening the **Distortion** curve (rack Extend 12303 → `__openCrvCard`, or Send To Shaper 35066) while popped: `openCrv(null)` hits the guard (35123) → `popOutCard('crv',0,0,0,0)` refocus ping → PE:5506-5522 re-fronts the window. One editor. ✓
- Opening a **different curve** (warp ⤢ → `__crvOpen(host)` 20149; mod ⤢ → 31606) while popped: `host` is non-null so the guard is skipped → `buildCrv`, `HOST=guest`, docked card `.open` (35134) → **two curve editors on screen** (the floating DST window + a docked guest card). The floating window is never told. This is the retarget hole any design must close.
- The main-view `crvXWin` poll (35245-35257) runs whenever `__poppedCards.crv` (35247): 150 ms `getDistortionCurves`, applies the blob to `CV` and repaints if the docked card is open — harmless to a guest (guest points live in closures) but it repaints the guest for nothing.

---

## 3 · How the popped crv window gets LIVE updates today

| lane | where | cadence | what |
|---|---|---|---|
| editor 60 Hz viz push | PE:6119-6126 | every timer tick while a `crv` window exists | `__crvPushT=Date.now(); __dstViz=o; __crvLiveTick(o)` — the occupancy/feed. `__crvLiveTick` (index.html:34613-34614) returns immediately unless `curHost().live` (DST only) |
| card self-poll | index.html:35219-35228 | 66 ms, only when the push lane is quiet >600 ms (editor closed) | `getDistortionCurveViz` → `__crvLiveTick` |
| blob relay | PE:6128-6147 | when `dstPtVersion_` bumps (any `setDistortionCurves`) | `__crvXApply(dj)` into main (6141) **and** the crv card (6142-6145); apply is compare/touch-guarded (35230-35242) |
| idle reconcile | index.html:35245-35257 | 150 ms in both windows (`need` 35247) | `getDistortionCurves` → `applyBlob` |
| every-card feed | PE:6089-6110 | 60 Hz | `__notesActive`, `__mvVel`, `__modViz(...)` to every card window (SafePointer-collected, fb525) |

All of it is **DST-shaped**: it pushes the distortion blob/viz and nothing else. A guest host needs its own truth lane:

- **WARP** — the follow-poll it already owns (20169-20191) *is* the lane; it just needs `getWarpCurve` on the card list. Pushed alternative: the editor timer could `evalJs` the slot's `getWarpCurveJson` at a low rate, but the poll is open-gated, one-owner (fb562) and retires by itself — keep it, and gate the two DST lanes (35219, 35245) on `curHost().key==='dst'` so they stop calling natives for a guest. Nothing the editor pushes is needed; the popped window outlives the editor anyway (PP.h:591-598).
- **MOD** — the mirror `restore()` (32196-32217, 2.5 s, 32220) already runs in card windows and already reads `o.c`; the popped editor's `a` is an entry of that mirror. Two gaps: `push()` is a no-op until `restoredOnce` (31098) — force `__tiModRestore()` at boot and open only after it lands; and the **main** view's restore merges DEPTH only for an existing route (32208 `if(ex){ ex.depth=…; return; }`), so a curve drawn in the popped window never lands in the docked `assigns[i].curve`, the row's ⤢ never lights, and the docked window's next `push()` (any depth drag) writes the stale curve back — the exact fb524 drift. Merge `c`/`b`/`x` for existing routes when `Date.now()-lastLocalEdit>1500` (the field already exists, 32195).

---

## 4 · Carrying the host SPEC to the popped window

Both designs share the **spec shape**: an identity, never points — `{key:'dst'}` · `{key:'warp',osc:'a',slot:0}` · `{key:'mod',s:<encodeSrc>,d:<dest>}`. Both hosts refetch truth from the processor (`getWarpCurve` / `getSynthMod`), which is what makes retargeting cheap and keeps the fb554/fb524 single-truth laws.

Both need a page-side **boot door** that replaces `__openCrvCard()` in the crv boot branch (35392-35395), e.g. `window.__crvBootHost(spec)`:
`dst` → `openCrv(null)`; `warp` → `__openWarpExt(osc, slot, {clientX:60,clientY:60,target:document.body})` (20200 — re-fetches, re-creates the host, restarts the one poll); `mod` → a new `__tiCurveEditKey(s,d)` beside `__tiCurveEdit` (31611) that calls `__tiModRestore()` (32219), waits for `findRoute(decodeSrc(s), d)` (31205/31186) to exist, then `openCurveEditor(a, document.body)`.

Both need the **same C++ additions on the card window's list** (PE:4993-5311):
1. `getWarpCurve` — copy of PE:3294-3302 (`proc.getWarpCurveJson(osc,slot)`, PP.h:644).
2. `setWarpDrawCurve` — copy of PE:3278-3293 (`proc.setWarpDrawCurve(osc,slot,csv,rate)`, PP.h:663).
3. A cardinality source, because `__paramCardinality` (19648-19658) reads relay properties and the card window has no relays (fb82; `PS()` 34306 already swaps to the shim for the same reason): either a tiny native `getParamCardinality(name)` → `RangedAudioParameter::getNumSteps()` on both lists, with `__paramCardinality` falling back to it when `__cardOnly`, **or** have the main view put `card:<n>` in the spec and let `writeMode` read `host.card` — the native is cleaner (works for the fb546 filter card too).
4. **Nothing for MOD** — `getSynthMod`/`setSynthMod` are there (5216/5221). `updateModConfig` is the FX-rack 3-LFO palette (PE:1912-1923; the card note at 5096-5098 says "NOT the 10-LFO synth strip") — leave it alone.

Both must fix the main view: the guard at 35123 must also cover `host!=null` while `__poppedCards.crv` (retarget instead of opening a docked twin), both toasts go (35335-35337, 35309-35311), and the two DST lanes (35219, 35245) get a host gate.

### Design A — spec in the URL (`?card=crv&spec=<urlencoded JSON>`)
- **Verified free:** the main provider ignores the URL (`getResource(url)` PE:6863 — "only one resource to serve", `url` is never read in 6863-6990) and the card provider ignores it outright (`[this](const auto&) { return html_; }` PE:5312-5315). The head sniff is `[?&]card=(…)` (455) so `?card=crv&spec=…` still boots card-only; the `page=` sniff (456) is untouched.
- C++: `popOutCard` gains a trailing string arg (PE:1195-1219 → `popOutCardWindow(..., bootSpec)` PE:5496 / PluginEditor.h:917) → ctor appends `"&spec=" + URL::addEscapeChars(bootSpec, true)` at PE:5365. No new natives beyond the three above.
- Page: `__crvBootHost(JSON.parse(decodeURIComponent(location.search.match(/[?&]spec=([^&]+)/)[1])))` in the boot branch.
- **Dock**: `__redockCard('crv')` (34270) must reopen the same host, but the URL is gone with the window — the main view has to keep `window.__crvLastSpec` when it pops (set at 35341). Lost on a wd9 reload of the main page and on editor close/reopen (`__poppedCards` is re-seeded from C++ at 35327, but a JS spec is not) → Dock after reopen lands on DST again.
- **Retarget** (open a different curve while popped): the URL is one-shot. You would need a new main native `retargetCard(id, spec)` that finds the window (PE:5549-5553 idiom) and `evalJs("__crvBootHost("+spec+")")` into it — at which point the spec is no longer "in the URL" anyway. Or close+re-pop (window flashes, position lost).
- Main view hide/show: unchanged from today (35342 hides; guard 35123 refocuses).
- Verdict: cheapest first boot; wrong shape for Dock-after-reopen and for retarget.

### Design B — spec on the processor (`cardBoot_` map, or the existing `cardStates_`)
- The processor already holds per-card JSON for exactly this purpose: `setCardStateJson/getCardStateJson` (PP.h:781-789, "both surfaces share ONE truth so pop-out / dock-back / editor reopen never lose the chain") with natives on **both** lists (`setCardState` PE:1029/5109, `getCardState` PE:1036/5115). Only the flow cards use it today (index.html:32980). Keying it `'crv'` (or a dedicated `'crvBoot'`) costs zero C++; a dedicated `cardBoot_` map + `getCardBoot` native is the same three lines if you want it separate from fb137 state.
- Flow: ⧉/edge-handoff → `setCardState('crv', spec)` (must complete before `popOutCard`; both natives are message-thread, calls are ordered on the same WebView channel) → `popOutCard('crv', …)` unchanged (PE:1195, whitelist 5503 unchanged, `goToURL` 5365 unchanged). Card boot → `getCardState('crv')` → `__crvBootHost(spec)`.
- **Dock**: `__redockCard('crv')` reads `getCardState('crv')` and boots the same host — survives wd9 reload and editor close/reopen (the window and its spec live on the processor, PP.h:591-598; dock with the editor closed is absorbed by the parked core, PP:543-544). Also lets `getPoppedCards` (PE:1232) stay as is.
- **Retarget**: main view writes the new spec and pings the window. Two ways: (i) a new main native `retargetCard(id)` that `evalJs("__crvBootHost("+proc.getCardStateJson("crv")+")")` into the crv window (PE:5549-5553 lookup + `evalJs` 4961); (ii) the card polls `getCardState('crv')` at ~300 ms and re-boots on change — an always-on interval in a card window (the fb90 law) but open-gated and one per window; (i) is cleaner and zero-cost at idle. Either way the floating window switches host in place — Serum-style, one editor, position kept.
- A bonus B enables: the editor's 60 Hz viz push (PE:6119-6126) can be gated on `proc.getCardStateJson("crv")` containing `"dst"` so a guest window stops receiving 60 evals/s of distortion JSON it discards at 34614.
- Main view hide/show: as A, plus the guard at 35123 becomes `if(!__cardOnly && __poppedCards.crv){ setCardState('crv', specOf(host)); retargetCard('crv'); return; }` for every host.

### Recommendation
**B.** It is the pattern the codebase already chose three times for exactly this problem (fb83 `getPoppedCards`, fb137 `cardStates_`, fb232 LFO blob: state that both surfaces need lives on the processor), it is the only one where Dock and retarget are correct after an editor close or a wd9 reload, and its C++ cost is one native lookup + one `evalJs`. A adds URL plumbing and still needs B's retarget native to meet Max's bar. Keep the spec an identity; keep `getWarpCurve`/`setWarpDrawCurve`/cardinality on the card list as the "both-lists law" (PE:5045-5047) demands — this is the fourth time that law has bitten (fb328, fb342, fb343 all cite it).

---

## 5 · Test plan

### What exists
- **`Tests/curve_probe.mm`** (header 1-24): one AU instance, measure → open the REAL editor windowless → run a gesture JS through the fb504 hook → close → measure again. The hook is `terrain-ui-exp.js` in `~/Library/Caches/Terrain Instrument` (curve_probe.mm:172-175; C++ side PE:5754-5777, fires once per open ~1 s after pageReady, `uiExpDone_` reset per open PE:14059). `CP_SET` seeds params (160-166), `CP_WATCH` prints params before/after (152-157), `CP_SETTLE` waits, `CP_REPORT` runs a second hook on a second open to collect an async trace (200; the page survives a close — the fb521 park). Line-buffered stdout (146). It exercises **the main page only** — the popped window is a second WebView with no hook.
- **`Tests/fb562_warpfollow.js`** (1-58): the template for a warp gate — drives `__setSynParam`, `__paramCardinality`, `__openWarpExt`, reads `.crv-ext .tt` and `__crvOpenState().pts`, wraps `setInterval` to count the 300 ms polls, reports via `window.__cpTrace` collected by `CP_REPORT`.
- **`Tests/mac_reopen.mm`** (1-30): raw AU host, windowless editor, close/reopen with the fb516 park; the readiness signal is the same hook file (18-20). Use it to prove popped windows and their spec outlive the editor.
- **Page gates** (`Tests/fb559_ui.js` 24-32, `Tests/fb560_marks.js` 20-27): puppeteer-core over `file://…/index.html` at 820×656, `pageerror` collected, 2.4-2.5 s settle, then `pg.evaluate` — un-hide `#syn-panel`, click `#syn-btn`, drive exported hooks (`__tiAddRoute` 31328, `__ulTick`, `__warpPicker`). Card-only is simulated by loading `index.html?card=crv` (the fb82 note's "headless verify"; the sniff at 455 only reads `location.search`). With no `window.Juce` every `NF()` is null and every native path no-ops (35287-35343 comments) — a gate can **install a stub `window.Juce.getNativeFunction`** before load (`page.evaluateOnNewDocument`, the `ui_trace.js` 50-70 idiom) to record calls and answer `getCardState`/`getWarpCurve`/`getSynthMod` with canned JSON.
- `Tests/floor_page.js` (1-30) hands a gate git HEAD's `index.html` as the pre-change page; `Tests/ui_trace.js` shows the `evaluateOnNewDocument` injection pattern.
- `Tests/README.md` has **no rows** for `curve_probe.mm`, `fb559_ui.js`, `fb560_marks.js`, `fb562_warpfollow.js` or `mac_reopen.mm` (grep count 0; the table is 13-30) — the new gate's row should bring those in.

### The new gate(s) — what must be asserted
A. **Page gate, main mode** (`index.html`, stub Juce): open a warp curve via `__openWarpExt('a',0,…)` with stubbed `getWarpCurve`; click `.crv-ext .pop`; assert (1) **no toast** (`.mv-toast` opacity stays 0 / textContent never becomes the fb560 string), (2) the stub saw `setCardState('crv', …)` with `{key:'warp',osc:'a',slot:0}` **before** `popOutCard('crv',…)`, (3) the docked card lost `.open` and `__poppedCards.crv===1`. Repeat for a mod route (`__tiAddRoute(0,1,64)` 31328 → `__tiCurveEdit(0)` 31611 → `.pop`) → spec `{key:'mod',s:…,d:64}`. Then `__redockCard('crv')` with the stub answering `getCardState` → `__crvOpenState().key==='warp'` and the title equals what was opened. Then, while `__poppedCards.crv` is set, open the mod curve → assert the stub saw a **retarget** (`setCardState`+`retargetCard`) and **no docked card opened** (`.crv-ext.open` absent). Mutation: put the fb560 toast back → bar 1 reds; drop the `setCardState` → bar 2 reds; revert the 35123 guard → bar 4 reds. Respect the 300 ms drag-click swallow (31545) — 350 ms between any synthetic drag and a click.
B. **Page gate, card mode** (`index.html?card=crv`, stub Juce answering `getCardState('crv')` with the warp spec and `getWarpCurve` with a fixture): after boot assert `.ti-popcard.crv-ext` exists, `.tt` reads `<mode> · OSC A · WARP 1 — Curve`, `__crvOpenState().pts` = the fixture's fitted count and `key==='warp'`; then `__crvSetPts([...])` (35164) and assert the stub recorded **`setWarpDrawCurve('a',0,<129 csv>,…)`** and a `setSynParam('SYN_OSC_A_WARP_MODE', …)` capture (which proves the card-side cardinality source works). Same for a mod spec: `getSynthMod` fixture with the route → title `LFO 1 — Curve` → `__crvSetPts` → stub recorded `setSynthMod` whose entry carries `c`. Count 300 ms intervals as fb562 does: exactly one. Mutation: remove `getParamCardinality` fallback → the warp capture bar reds.
C. **Both-lists gate** (python, the `wt_list_gate.py` idiom): parse PE and assert every native the curve editor calls from a card window (`getDistortionCurves`, `setDistortionCurves`, `getDistortionCurveViz`, `getSynthMod`, `setSynthMod`, `setSynParam`, `getSynParam`, `getCardState`, `getWarpCurve`, `setWarpDrawCurve`, `getParamCardinality`) appears inside `TerrainCardWindow`'s list (PE:4959-5367) — the structural fix for the law at PE:5045-5047.
D. **End-to-end via curve_probe** (the sound): `CP_SET` a warp slot to Fractalize at amount 0.6; gesture JS pops the warp curve (`__openWarpExt` → simulate the ⧉ path by calling `NF('setCardState')` + `NF('popOutCard')('crv',100,100,514,400)` directly), waits, then asserts via `NF('getPoppedCards')()` that `crv` is floating and `document.querySelector('.crv-ext').classList.contains('open')===false`; `CP_REPORT` on the second open asserts `getPoppedCards` still says `crv` (processor-owned) and `__redockCard('crv')` yields `__crvOpenState().key==='warp'`. The **popped page itself is unreachable** from every harness today (its WebView has no hook: PE:5754 is the main view's timer) — either accept the A+B+C split, or add the fb504 hook to `TerrainCardWindow::timerCallback` (PE:5424) reading `terrain-card-exp.js` once (same diagnostic-only idiom), which would let curve_probe drive a point move in the real popped window and measure the spectrum delta on the same AU instance. Read `~/Library/WavesCrate/TerrainInstrument/cardwin.log` ("created crv", "docking crv") as the window-server-side confirmation (PE:5364, PP:540).
E. **Reopen** (`mac_reopen.mm` lineage): pop a warp curve, close the editor, reopen, `__redockCard('crv')` → same host — only B passes this.

---

## 6 · Risks

1. **fb90 — a card window is a full hidden index.html.** Every always-on loop is gated by `__cardOnly` (e.g. 8526, 8537, 12201, 14347, 22319, 30850, 36838). Guest hosts add: the warp poll (20172, 300 ms — open-gated, one-owner, retires on close: OK), the mod `restore()` interval (32220, 2.5 s — already runs in card windows), and the two DST lanes that will now run for nothing in a guest window: the 66 ms self-poll (35219-35228) and the 150 ms `crvXWin` (35245-35257) — gate both on `curHost().key==='dst'`. In B, gate the editor's 60 Hz viz push (PE:6119) on the spec. `stampMorphDest` (34669-34673, 1.2 s DOM query) already runs in the card — pre-existing. Do not add a card-side spec poll if you take retarget-by-evalJs.
2. **fb83 — never become key.** Unchanged: `windowIgnoresKeyPresses` (PE:5397-5400), `toFront(false)` (5332, 5521). The curve editor needs no keyboard; the mod host's picker/menus are pointer-only. Do not add `editArm` calls to the hero.
3. **fb178 capture guard / 300 ms drag-click swallow** (31545): applies to `#syn-panel .syn-ctx-item` etc. In card-only the `#syn-ctx-menu` branch is kept laid out as `.ti-ghost` (35408) so the crv card's right-click menu still lives inside `#syn-panel` — a gate that drags a point and then clicks a menu row within 300 ms sees nothing. Wait 350 ms (the fb564 law).
4. **`__paramCardinality` is 0 in a card window** (19648-19658 reads relay `properties`; fb82: a relay serves one WebView). The warp `push()` returns at 20117 without toast → a capture in the popped window would be a *silent* no-op (the fb562 "feature that never ran" class). Add the native and a gate (5-B).
5. **openCrv main-view assumptions:** placement anchor `#syn-panel .fxr-core[data-core="saturate"]` with a null-safe fallback (35130-35131) and the rAF clamp (35135-35142) are neutralised by the `!important` pin (3471) — fine. `buildCrv` appends to `#syn-panel` only when not card-only (35075). The rack mini-viz mirror `drawnC` returns null for any guest (34659) and the rack tick is `__cardOnly`-gated anyway (12201). `setTypeUI`/`toggleSym` have card-only branches through `PS` (35068-35083). `sendToShaper` forces `HOST=null` (35060) — a Send To Shaper in a popped guest window would flip the window to DST silently; it is only reachable from the DST host's `capture` (34370), so fine, but keep it that way.
6. **MOD mirror drift.** `restore()` merges depth only for existing routes (32208) → a curve edited in the popped window never reaches the docked `assigns`, and the docked window's next `push()` (31094) resurrects the old curve. Also the card-only prune (32213-32215) can delete the route the popped host holds — the host's `a` becomes an orphan and `push()` writes to it; watch `findRoute` on each restore and close/toast when the route is gone. `push()` is gated on `restoredOnce` (31098) — boot must force `__tiModRestore()` (32219).
7. **`__tiQuiet` is Windows-only** (index.html:36-56): on Windows the card's polls (`crvXWin` 35248, `restore` 32220) pause after 1.5 s without notes/input — a mod boot waiting for `restore()` can stall; call `restore()` directly rather than waiting for the interval.
8. **Two whitelists** (PE:5503-5504 and index.html:455): unchanged by A or B because the id stays `crv`; a new id would need both (the fb231 disappear-on-drag trap).
9. **Ordering on pop:** `setCardState` must land before the card boots — both natives are message-thread and the card page only boots after `goToURL` (5365) + ~60 ms (35439), so a `setCardState` issued before `popOutCard` on the same channel is safe; do not fire them from two different windows.
10. **A-only:** JSON in a custom-scheme URL must be `URL::addEscapeChars`'d; the sniff regexes at 455-456 are anchored on `[?&]` so `&spec=` cannot collide; a second `?card=` in the spec string would be caught by the first match — escape it.
11. **Popped windows outlive the editor** (PP.h:591-598; dtor clears PP:486-489). A design that keeps the spec in a JS variable of the main page loses it on close/reopen and on wd9 reload (`__poppedCards` is re-seeded at 35327, a spec would not be) — B keeps it on the processor.
12. **The popped page is untestable by every harness today** (5-D). Decide up front whether to add the card-window exp hook (PE:5424) or to accept the page-gate + both-lists-gate proof; do not ship with only the main-page gesture green (fb373: "a green harness proves the ENGINE, never that the plugin REACHES it").
