# LFO head "static clicks out" at the last note-end — root cause (no fix applied)

Max (2026-09-02): *"the LFO only moves whenever we're clicking the MIDI — I like that. The only thing is that whenever it leaves, it just static clicks out. It's supposed to fade away, like how it fades in whenever we have it."*

All paths below are under `plugins/TerrainInstrument/`; `index.html` = `Source/ui/public/index.html`.
Probe: `/private/tmp/claude-501/-Users-macshooter/1ed797b9-dcbc-43df-879e-d65e0cbf3bc8/scratchpad/lfo_fade_probe.js` (real headless Chromium via puppeteer-core, the same harness as `Tests/lfo_park.js`; run line at the end).

## Headline

The fade-OUT never got its 350 ms. The rule that is supposed to dissolve the head — `.mv-idle .mv-play,.mv-idle .mv-foll{opacity:0;transition:opacity .35s ease}` (index.html:2881, specificity 0,2,0) — loses the `transition` property to the base rule on the previous line, `#mod-engine .mv-play,#mod-engine .mv-foll,.mv-ext .es …,.lfo-ext .card-scope …{transition:opacity .12s ease}` (index.html:2880, specificity 1,1,0 / 0,3,0). Only `opacity:0` survives from the idle rule, so both edges run the 120 ms "must feel instant" fade-in curve. Measured in Chromium: computed `transition-duration` in the idle state is **0.12s** on the dot and the line; opacity walks 1.00 → 0.84 (49 ms) → 0.15 (100 ms) → 0.00 (151 ms). The same 120 ms at note-on is hidden by the dot arriving *in motion*; at note-off the dot is stationary, so 120 ms reads as a click. Injecting a higher-specificity idle rule into the live page (no other change) gives the intended walk: 0.97 (47 ms) → 0.42 (148 ms) → 0.04 (299 ms) → 0 at 349 ms.

Nothing else moves at the park edge in the default (Free) trigger mode: same DOM node before/after, zero innerHTML rebuilds, no inline styles, no per-frame opacity writes. One secondary mechanism exists for Trig/Env modes only (a DSP phase-source switch on the parking frame, §H-4).

## What the probe measured (unmodified index.html, Chromium)

| bar | result |
|---|---|
| A live | idle=false, opacity 1/1, **transition-duration dot=0.12s line=0.12s**, stroke animation `mvBreathe` |
| B park edge | class landed (idle=true); **computed transition-duration in idle: dot=0.12s line=0.12s**; dot node tag `A` (same node), line tag `A`; scope childList rebuilds **0**; head attribute writes **4** (x1,x2,cx,cy from the one last placement); inline style `null`/`null`; cx 228.0 → 228.0 (same phase pushed) |
| B opacity walk | t=0.4 ms 1.000 · 49 ms 0.837 · 67 ms 0.531 · 84 ms 0.295 · 100 ms 0.148 · 117 ms 0.062 · 134 ms 0.017 · **151 ms 0.000** · 299 ms 0.000 → verdict "FADE (reaches 0 at 151 ms)" — a fade, but the 120 ms one |
| B stroke | opacity 0.988 → 0.989, animation `mvBreathe` → `mvBreathe` (the breathing does NOT stop at idle — see §H-3) |
| C wake edge | transition-duration 0.12s; 0.000 → 0.164 (50 ms) → 1.000 at 151 ms — the fade-in Max likes is the same 120 ms curve |
| D phase-switch flip | live phase 0.75, parking frame carries 0.20: **cx 342.0 → 91.2 px** — the page places the head one last time with whatever phase the flip frame carries |
| E control | inject `#mod-engine.mv-idle .mv-play,#mod-engine.mv-idle .mv-foll{transition:opacity .35s ease}` → idle transition-duration **0.35s**; walk 1.000 · 0.969 (47) · 0.424 (148) · 0.036 (299) · 0 at 349 ms |
| F control | the SHIPPED rule with `!important` → identical 0.35s walk (proves the shipped declaration is correct and merely unreachable) |
| G control | `animation-play-state:paused` on the stroke instead of `animation:none` → 0.916 → 0.915 (holds, no jump) |
| page errors | none |

(The ~33 ms plateau at 1.000 before the ramp is headless Chromium's style/paint scheduling after the class write, not a page defect; it appears identically in the controls.)

## Ranked hypotheses

### H-1 (ROOT CAUSE, confirmed) — the .35 s fade-out is dead CSS: specificity
- index.html:2880 `#mod-engine .mv-play,#mod-engine .mv-foll,.mv-ext .es .mv-play,.mv-ext .es .mv-foll,.lfo-ext .card-scope .mv-play,.lfo-ext .card-scope .mv-foll{transition:opacity .12s ease;}` — specificity (1,1,0) for the panel, (0,3,0) for both cards.
- index.html:2881 `.mv-idle .mv-play,.mv-idle .mv-foll{opacity:0;transition:opacity .35s ease;}` — (0,2,0). `.mv-idle` is written on `#mod-engine` itself and on the card root (index.html:30468 `root.classList.toggle('mv-idle', on); if(ext) ext.classList.toggle('mv-idle', on)`; the card root is `<div class="ti-card lfo-ext">`, index.html:32604 via 30295 `T.shell({ cls:'lfo-ext', …})`).
- Cascade: for every surface the base rule outranks the idle rule on `transition`; `opacity:0` is uncontested (index.html:2870 `#mod-engine .mv-play{stroke:…;stroke-width:1;}` and 2876 `#mod-engine .mv-foll{fill:#FFFFFF;filter:…}` set no opacity). CSS transitions take their duration from the after-change style, so both add and remove of `.mv-idle` run 120 ms. The comment on 2880 says why 120 ms feels like a click: "the fade IN: a note-on must feel instant".
- Probe A/B/E/F above. `Tests/lfo_park.js:199` bar 3 only asserts `fadeAt < 600` ms, so the gate cannot tell a 120 ms fade from a 350 ms one — which is how fb567 shipped green.

### H-2 (falsified for the main panel) — (a) the SVG is rebuilt at/after the park edge
- Rebuild sites: `render()` index.html:30202 `root.innerHTML=front()` (front() embeds `scopeSVG()`, 30192); `shPaintAll()` 29558-29559; ext phase drag 30346 `scM.innerHTML=scopeSVG()`; `extRefresh0()` 30369 `hero.innerHTML=scopeSVG()`; the builders 29612-29620 `pathSVG`, 29700-29702 `shaperSVG`, 30132-30137 `waveSVG`.
- Their callers are all gestures/menus/param listeners (29775-30103, 30221-30242, 30303-30318) or the popped-card mirror poll (30420-30436: only when `__cardOnly==='lfo'||__poppedCards.lfo`, and `render()` only when the shapes JSON changed, `if(!j||j===last)return`). The param listeners (30169, 30533) fire on LFO shape/sync/div/rate/depth/phase writes — nothing writes those at a note-end.
- The parking pass itself (index.html:30481-30524) touches only attributes: `shGhostTick` 29947-29950 sets `.sh-ghost` `d`; `shTrajTick` 29658-29677 sets `.mv-ctrail`/`.mv-traj` `d`; `placeHead` 30446-30460 sets x1/x2/cx/cy; the emblem 30521-30524 rewrites `#mv-shape .ic` (the footer icon, not the scope).
- Probe B: same node (tag preserved), 0 childList mutations under `.mv-scope` and on `#mod-engine`, 4 attribute writes.
- Residual risk (not the reported bug): with a popped LFO window on the Mac, the 150 ms mirror poll is ungated (`window.__tiQuiet` is never assigned on the Mac — index.html:56 lives inside the Windows-only IIFE that returns at :23) and would rebuild both scopes mid-fade if the blob changed at that instant; a rebuilt head is born at opacity 0 inside an idle root (no previous computed value → no transition).

### H-3 (falsified as a cause; a side finding) — (c) the stroke's breathing snaps at the edge
- index.html:2882 `.mv-idle .mv-stroke{animation:none;}` (0,2,0) loses to 2873-2875 `#mod-engine .mv-stroke{…animation:mvBreathe 3.2s ease-in-out infinite;}` (1,1,0) and 2937 `.mv-ext .es .mv-stroke{…animation:mvBreathe…}` (0,3,0). Probe B/C: `animationName` stays `mvBreathe` in idle, stroke opacity 0.988 → 0.989 (no blink). So there is no curve blink — but fb567's claim that "the curve's breathing rests with the head" (memory note, index.html:2882 comment) is not true on the Mac; the breathing keeps ticking in silence. On Windows it is dead anyway (index.html:232 `#ti-static-decor … animation:none !important`, UA-gated at :23). Same root as H-1; if the rest is wanted, `animation-play-state:paused` holds the mid-cycle value with no jump (probe G).

### H-4 (real, but Trig/Env modes only) — (d)/(e) the DSP switches the published phase on the parking frame, and the page's "one last placement" follows it
- PluginProcessor.cpp:10735-10740: `const bool vTrig = synModCfg.lfos[k].trigger != wc::LFOTrigger::Free; modVizLfoPh_[k].store((vTrig && any && bestVoice != nullptr) ? bestVoice->lfoPhase(k) : flowLfo_[k].currentPhase(), …)`. `any` is the same voice-walk flag that drives `ampEnvVis` (10723 `ampEnvVis.store(any ? best : -1.f, …)`, walked at 10689-10705 over `sv->isAmpEnvActive()`, SynthVoice.h:436). So in the very block the flag goes false, a Trig/Env LFO's published phase jumps from the last voice's phase (SynthVoice.h:714) to the global bank's.
- PluginEditor.cpp:5951-5952 writes `window.__notesActive=<ampEnvVis>=0>;window.__notesActiveT=Date.now()` outside the quiet gate, and 5970-5975/5997 ship `pArr` (`modVizLfoPh(k)`) via `__modViz` inside `!uiQuiet` in the same frame. That frame's bytes differ, so it ships and ends with `__tiFrame()` (6667) → one rAF (index.html:370-375) → `lfoFrame`.
- index.html:30475 `live = !!__notesActive && !!naT && (Date.now()-naT) < 700` → 30481 parks (`lfoIdle(true)`) and then CONTINUES: 30486-30489 reads the fresh pushed phase into `ph`, 30513-30514 `placeHead(root)` moves the head. Probe D: a switched phase moves the head 342 → 91 px at the same instant the fade starts. That is a genuine "jump then vanish".
- Default trigger is Free: index.html:29561 `moGet()` default `tg:0`, PluginProcessor.cpp:9297-9302 maps `tg 0 → LFOTrigger::Free`; Free's bank phase is a HOLD in silence (10004-10013, fb566: "no reset, no phase jump"). So Max only sees this if he set Trig/Env on the card (index.html:30309-30310). Not the primary cause, but it is the second thing to fix if he did.

### H-5 (falsified) — (b) inline style / removal on the park edge
- `lfoIdle()` index.html:30467-30468 is one `classList.toggle` per edge, in a try/catch; `placeHead` writes only geometry attributes. Probe B: `style` attribute `null` on both, node retained.

### H-6 (falsified) — (d) the flag flaps at the tail
- `ampEnvVis` is `any ? best : -1` from a single walk over `isAmpEnvActive()` (PluginProcessor.cpp:10689-10705, 10723) — no hysteresis needed, no flap: it is false exactly when the last voice's amp envelope leaves its active state, release tail included. `velVis_` (10710) and `flowAnySounding` (9956) share the walk but do not feed the flag. The page's `__notesActive` setter (index.html:159-165) only calls `act()` on a 0→1 edge.

### H-7 (falsified) — (e) the class lands late / the head moves after the class
- Push mode: the flip frame ships (bytes changed) and dispatches once; every later `lfoFrame` returns at index.html:30481 `if(__lfoIdle) return` before the DOM (probe: `Tests/lfo_park.js` bar 4 — 0 writes over 60 dispatches). Frames that keep shipping during an FX tail (`eqQuietTicks_` resets while audible, PluginEditor.cpp:6471) are harmless for the same reason. Order inside one frame: class first (30481), then the one placement (30513) — the placement is the same push's phase, cx unchanged in probe B. Only the phase-source switch of H-4 makes that placement a jump.

### H-8 (falsified) — (f) something hides the panel wholesale at idle
- Only three `.mv-idle` rules exist (index.html:2881-2882); no `display`/`visibility`/`.ti-ghost` rule keys on it. `__tiOff('mod-engine')` (index.html:5844-5854, called at 30484) gates painting, not visibility, and the park runs before it.

### H-9 (falsified) — (g) a painter writes opacity per frame
- `#mv-fd`/`#mv-ph` appear only in the three builders and `placeHead` (grep); `.mv-foll`/`.mv-play` only in CSS. No `setAttribute('opacity')`/`style.opacity` in the module (the only one is the toast, 30248). MutationObserver with `attributeFilter:['style','opacity',…]` saw none.

## The single most likely root cause

index.html:2880-2881 — the idle rule's `transition:opacity .35s` is outranked by the base rule's `transition:opacity .12s` on every surface (panel: ID selector 1,1,0 vs 0,2,0; both cards: 0,3,0 vs 0,2,0). The head therefore vanishes on the note-on curve (120 ms, "must feel instant") while standing still. fb567's own gate could not catch it (`lfo_park.js:199` accepts anything under 600 ms).

## Minimal fix (one change, at the root — NOT applied)

Give the idle declarations the same anchors as the base rules, so they win by specificity where they already win by order. Replace index.html:2881 (and, for the same reason, :2882) with:

```css
#mod-engine.mv-idle .mv-play,#mod-engine.mv-idle .mv-foll,
.lfo-ext.mv-idle .card-scope .mv-play,.lfo-ext.mv-idle .card-scope .mv-foll,
.mv-ext.mv-idle .es .mv-play,.mv-ext.mv-idle .es .mv-foll{opacity:0;transition:opacity .35s ease;}
#mod-engine.mv-idle .mv-stroke,.mv-ext.mv-idle .es .mv-stroke{animation-play-state:paused;}   /* holds the breath mid-cycle: no jump (probe G) */
```

(Equivalent one-liner alternative: keep 2881 and put the duration in an inherited custom property — `transition:opacity var(--mv-fade,.12s) ease` on 2880 and `.mv-idle{--mv-fade:.35s}` — custom properties inherit from the root the class lands on, so specificity cannot intercept them.)

Then add a bar to `Tests/lfo_park.js` that reads `getComputedStyle(fd).transitionDuration === '0.35s'` after the edge and asserts opacity > 0.3 at ~150 ms — the probe's E/F walk is the expected shape.

If Max is in Trig/Env mode, the second (DSP) change is PluginProcessor.cpp:10738-10740: when `!any`, keep the last published per-voice phase (do not switch to `flowLfo_[k].currentPhase()`), so the parking frame carries the held phase and the last placement is a no-op.

## Open questions

1. WebKit: the probe runs Chromium; the plugin renders in a WKWebView. The cascade is identical (H-1 holds), but whether WebKit adds anything on top (e.g. an opacity transition on a `filter: drop-shadow` SVG circle, index.html:2876) was not measured — an in-plugin `getComputedStyle` sample via a `Tests/mac_idle_frames.mm`-style eval would close it.
2. Which trigger mode Max was in (Free = default; Trig/Env adds the H-4 jump).
3. Does Max want the curve's breathing to rest in silence? fb567 promised it (index.html:2882 comment, memory note) but the CSS never did it on the Mac (H-3).
4. Popped-card mirror poll (index.html:30420-30436) is ungated on the Mac; a shapes-blob change inside the 350 ms window would rebuild the head at opacity 0 (H-2 residual).

## Run line

```
NODE_PATH=/private/tmp/claude-501/-Users-macshooter-Developer-VST-Plugins/de960951-e6c2-440a-ab03-7d0b0afd2693/scratchpad/node_modules \
node /private/tmp/claude-501/-Users-macshooter/1ed797b9-dcbc-43df-879e-d65e0cbf3bc8/scratchpad/lfo_fade_probe.js
```
