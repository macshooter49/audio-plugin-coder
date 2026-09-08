// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb563_menu.js — ONE RIGHT-CLICK MENU ON EVERY CONTROL.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/fb563_menu.js [page.html]
//
//  Max: "every button, every parameter on Serum has this right-click menu... I want to have
//  something similar... it needs to be very simple, like Serum 2 is very simple... this has to
//  be the menu even for macros... the menu should not overlap or get cut off at the bottom."
//
//  🚨 THE VIEWPORT IS THE SHIPPED ONE (fb454's law): 820 × 656. The rack sits on its bottom edge,
//  which is where "never cut off" is actually tested.
//
//  THE BARS
//   1  ONE MENU EVERYWHERE — every visible modulation destination on the page (every synth knob,
//      every pill, the filter emblems, the LFO depth ring, the rack's knobs front AND back, a
//      docked flow card's cells) answers a REAL contextmenu event with exactly one house menu that
//      carries "Modulate" and "Reset to default", opens no second menu or browser, and sits fully
//      inside the panel. Not a sample: the whole list targets() advertises.
//   2  THE PICKER ROUTES — Modulate › opens the two-pane browser with the families All · Envelopes
//      · LFOs · Keys · Audio; clicking LFO 2 makes the route {lfo2, dest, 0.5} on the SAME matrix
//      the drag uses (__tiRoutes); clicking it again removes it; Env 1 lands at 1.0; search finds
//      "Filter 2" and routes the seventh follower — the wire code the old decoder could not read.
//   3  THE ROUTE ROW — reopening the menu shows one row per connection with the depth slider; a
//      drag on the slider changes the route's depth; ⤢ opens the house curve editor ON THAT ROUTE;
//      ✕ removes it.
//   4  CONTROL ROWS FOLD IN — Warp A carries "Warp mode" naming the current mode and that row opens
//      the warp browser, whose pick writes SYN_OSC_A_WARP_MODE; Fold A carries "Fold shape"; the
//      filter's back Drive knob "Drive type"; a blend pill "Blend mode"; the rack Filter's Drive
//      knob "Drive type"; and the Keytrack knob — NOT a destination — still gets the one menu with
//      "Keytrack target" and without "Modulate".
//   5  RESET — "Reset to default" writes the knob's registered default (Warp A → 0) and a rack
//      dial's template default, exactly as a double-click does.
//   6  COPY / PASTE — two routes copied from Warp A land on Fold A with their depths.
//   7  THE CODEC — window.__modWire decodes 215/216 as Filter 1/2, 201 as Key, 200 as Velocity,
//      rejects 217, 132 and 10, and encode∘decode is the identity on every accepted code.
//   8  NO REGRESSION — zero page errors; the fb554 "· Curve" rows are gone; a right-click on the
//      envelope graph (not a destination) still opens the ENVELOPE menu through its own handler.
//  12  THE GRID (fb564) — no header, no separators; every word on ONE left rail; every value on ONE
//      right edge (tabular); ⤢ · ✕ · › are identical 16 px boxes sharing ONE right rail; 24 px rows.
//      A double-click on a picker source ASSIGNS it and closes (it used to toggle off and close).
//      A click on a route row opens its options (Bypass · Scale by · Remove) — no title, same grid.
//  13  A MACRO IS A CONTROL (fb564) — right-click a Macros-view knob → Rename · Reset to default ·
//      MIDI Learn (arming SYN_MACRO_n); a rename renames the picker entry, the route row and the
//      hover list, and reaches the processor (setMacroNames).
//  14  COPY WITH MODULATORS (fb564) — the osc menu offers Copy oscillator · Copy with modulators ·
//      Paste oscillator; the paste calls copyOscParams(a,b) and re-aims every route on A's knobs at
//      B's twin knobs with the same depths.
//
//  PROOF THE BARS CAN FAIL (fb421 — a gate that has never failed has never been tested):
//    FB563_MUTATE=1  deletes the capture listener                     → bars 1-6 red
//    FB563_MUTATE=2  the picker only ever adds (never toggles off)     → bar 2 red
//    FB563_MUTATE=3  the registry returns no rows                       → bar 4 red
//    FB563_MUTATE=4  followers bounded at five again (the shipped bug)  → bars 2 and 7 red
//    FB563_MUTATE=5  the picker's double-click fix removed (fb564)       → bar 12 red
//    FB563_MUTATE=6  the ✕ / ⤢ boxes shrunk to 10 px (fb564)            → bar 12 red
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core');
const fs = require('fs'), path = require('path'), os = require('os');
const ROOT = path.join(__dirname, '..');
const PAGE = process.argv[2] || path.join(ROOT, 'Source/ui/public/index.html');
const MUT = +(process.env.FB563_MUTATE || 0);
const VW = 820, VH = 656;
let pass = 0, fail = 0;
const chk = (ok, l, d) => { if (ok) { pass++; console.log('  ok    ' + l + (d ? '   ' + d : '')); }
                            else { fail++; console.log('  FAIL  ' + l + (d ? '   ' + d : '')); } };

function mutatedPage () {
  if (! MUT) return PAGE;
  let src = fs.readFileSync(PAGE, 'utf8');
  const sub = (f, t) => { const n = src.split(f).length - 1;
    if (n !== 1) { console.error('MUTATION ' + MUT + ': anchor matched ' + n + ' times -> ' + f.slice(0, 90)); process.exit(2); }
    src = src.replace(f, t); console.log('  (mutation ' + MUT + ' landed: 1 site)'); };
  if (MUT === 1) sub("    hideRoutes(); openCtlMenu(g,ev);\n  },true);", "    /* mutation: no menu */\n  },true);");
  if (MUT === 2) sub("if(ex){ removeAssign(ex); o.sel=false; } else { addRoute(S,g.dest,g.el); o.sel=true; }",
                     "addRoute(S,g.dest,g.el); o.sel=true;");
  if (MUT === 3) sub("    rowsFor: function (el, ev) { var out = [], n = el, hops = 0;", "    rowsFor: function (el, ev) { return []; var out = [], n = el, hops = 0;");
  if (MUT === 4) sub("var WIRE={ENV:100, VEL:200, NOTE:201, FOL:210, NFOL:7, MACRO:220,", "var WIRE={ENV:100, VEL:200, NOTE:201, FOL:210, NFOL:5, MACRO:220,");
  if (MUT === 5) { sub("            if (cfg.multi && ev && ev.detail >= 2) { if (! it.sel) { try { it.pick (); } catch (e) {} } close (); return; }\n            try { it.pick (); } catch (e) {}   // single-click",
                        "            try { it.pick (); } catch (e) {}   // single-click");
                   sub("clearTimeout (window.__tpbPrevT); if (cfg.multi && ! it.sel) { try { it.pick (); } catch (e) {} } close (); };", "clearTimeout (window.__tpbPrevT); close (); };"); }
  if (MUT === 6) sub(".ctl-rx { width: 16px; height: 16px; display: grid;", ".ctl-rx { width: 10px; height: 10px; display: grid;");
  const p = path.join(os.tmpdir(), 'fb563_mut' + MUT + '.html'); fs.writeFileSync(p, src); return p;
}

// a recording stub: every parameter write lands in __emits with its name and value, choice
// params snap on their cardinality (the warp lane's is read out of the C++, never retyped).
const WARP_N = (() => {   // the warp lane's cardinality, read from the C++ (all_menus.js's own extraction)
  const s = fs.readFileSync(path.join(ROOT, 'Source/PluginProcessor.cpp'), 'utf8');
  const m = /for \(int i = w\.size\(\); i < (\d+); \+\+i\) w\.add \("Reserved "/.exec(s);
  if (! m) throw new Error('warp cardinality not found in PluginProcessor.cpp');
  return +m[1];
})();
const STUB = (cfg) => {
  window.__emits = []; window.__natives = [];
  const CH = cfg.choice; const states = new Map();
  const mk = (name) => { const n = CH[name] || 0;
    const props = n ? { start:0, end:n-1, skew:1, name, label:'', numSteps:n, interval:1, parameterIndex:states.size }
                    : { start:0, end:1, skew:1, name, label:'', numSteps:100, interval:0, parameterIndex:states.size };
    const st = { name, norm:(name === 'SYN_BEND_RANGE' ? 2/24 : 0), properties:props,   // the bend range's registered default (2 st) — the only non-zero default the bars read back
      getScaledValue(){ return n ? Math.round(st.norm*(n-1)) : st.norm; },
      setScaledValue(v){ st.norm = n ? v/(n-1) : v; },
      getNormalisedValue(){ return st.norm; },
      setNormalisedValue(v){ st.norm = n ? Math.round(v*(n-1))/(n-1) : v;
        window.__emits.push({ name, norm:+(+v).toFixed(6) }); (st.__ls||[]).forEach(f => { try { f(); } catch(e){} }); },
      valueChangedEvent:{ addListener(f){ (st.__ls = st.__ls || []).push(f); return {remove(){}}; }, removeListener(){} },
      propertiesChangedEvent:{ addListener(){ return {remove(){}}; }, removeListener(){} },
      getChoiceIndex(){ return Math.round(st.norm*(n-1)); }, setChoiceIndex(i){ st.norm = i/(n-1); },
      getValue:()=>false, setValue(){}, sliderDragStarted(){}, sliderDragEnded(){} };
    return st; };
  const get = (name) => { if (! states.has(name)) states.set(name, mk(name)); return states.get(name); };
  window.__stubState = get;
  const nativeFn = (nm) => (...a) => new Promise((r) => { window.__natives.push({ fn:nm, args:a.map(String) });
    if (/getPresets/i.test(nm)) return r('[]'); if (/getSynthMod$/.test(nm)) return r('[]');
    if (/^getSynParam$/.test(nm)) return r(String(get(String(a[0])).getNormalisedValue()));   // the relay-free read-back, from the same stub state the writes land in
    if (/Json|JSON/.test(nm)) return r('{}'); r(0); });
  window.Juce = { getSliderState:get, getToggleState:get, getComboBoxState:get, getNativeFunction:nativeFn,
                  backend:{ addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  (function(){ const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable:true,
    get(){ return held; }, set(v){ held = Object.assign({}, v||{}, { getNativeFunction:mine.getNativeFunction, getSliderState:mine.getSliderState }); } }); })();
  window.__JUCE__ = { backend:window.Juce.backend, initialisationData:{ vendor:'', pluginName:'', pluginVersion:'',
    __juce__sliders:[], __juce__toggles:[], __juce__comboBoxes:[], __juce__functions:[] } };
  Element.prototype.setPointerCapture = function(){}; Element.prototype.releasePointerCapture = function(){};
  window.__errStacks = []; window.addEventListener('error', (e) => { try { window.__errStacks.push(String((e.error && e.error.stack) || e.message).slice(0, 400)); } catch(x){} });
};

// in-page helpers — every bar drives the SHIPPED door (a real contextmenu event on the element)
const HELPERS = () => {
  const M = () => document.getElementById('syn-ctx-menu');
  window.__mOpen = (el, x, y) => {
    if (! el) return 'no element';
    const r = el.getBoundingClientRect(); x = x != null ? x : r.left + r.width/2; y = y != null ? y : r.top + r.height/2;
    el.dispatchEvent(new MouseEvent('contextmenu', { bubbles:true, cancelable:true, clientX:x, clientY:y, view:window }));
    return window.__mState(); };
  window.__mState = () => { const m = M(), act = !!(m && m.classList.contains('act'));
    const rows = act ? [...m.querySelectorAll('.syn-ctx-item')].map(d => d.textContent.trim()) : [];
    const routes = act ? [...m.querySelectorAll('.syn-ctx-route')].map(d => d.querySelector('.ctl-name').textContent.trim() + ' · ' + d.querySelector('.ctl-val').textContent.trim()) : [];   /* fb564 — name and value are two columns; spelled as one line here */
    const cls = act ? m.className : ''; const seps = act ? m.querySelectorAll('.syn-ctx-sep').length : 0;
    const head = act && m.querySelector('.syn-ctx-header') ? m.querySelector('.syn-ctx-header').textContent.trim() : '';
    const browser = !!document.querySelector('.tpb-panel');
    const rect = act ? (() => { const q = m.getBoundingClientRect(); return [q.left, q.top, q.right, q.bottom].map(v => +v.toFixed(1)); })() : null;
    return { act, head, rows, routes, browser, rect, cls, seps }; };
  window.__mRow = (prefix) => { const m = M(); if (! m) return false;
    const d = [...m.querySelectorAll('.syn-ctx-item')].find(x => x.textContent.trim().startsWith(prefix)); if (! d) return false; d.click(); return true; };
  window.__mClose = () => { try { window.__synHideMenu(); } catch(e){} try { if (window.__tpbClose) window.__tpbClose(); } catch(e){} };
  window.__bPanes = () => [...document.querySelectorAll('.tpb-pane')];
  window.__bRows = (pane) => [...pane.children].map(d => ({ el:d, name:(d.querySelector('span[style*="flex:1"]')||d).textContent.trim(), dot:!!d.querySelector('span') && d.querySelector('span').textContent.trim() === '•' }));
  window.__bCats = () => { const p = window.__bPanes(); return p.length < 2 ? null : window.__bRows(p[0]).map(r => r.name); };
  window.__bPick = (cat, name) => { const p = window.__bPanes(); if (p.length < 2) return 'no browser';
    const c = window.__bRows(p[0]).find(r => r.name === cat); if (! c) return 'no cat ' + cat; c.el.click();
    const it = window.__bRows(window.__bPanes()[1]).find(r => r.name === name); if (! it) return 'no item ' + name; it.el.click();
    const after = window.__bRows(window.__bPanes()[1]).find(r => r.name === name); return after ? (after.dot ? 'dot' : 'nodot') : 'gone'; };
  window.__bSearch = (q, name) => { const s = document.querySelector('.tpb-srch'); if (! s) return 'no search'; s.value = q; s.oninput();
    const it = window.__bRows(window.__bPanes()[1]).find(r => r.name === name); if (! it) return 'no hit ' + name; it.el.click(); return 'picked'; };
  window.__routes = () => window.__tiRoutes();
  window.__clearRoutes = () => { window.__tiPruneFxRoutes(0, 1e9); };
  window.__valDrag = (i, dy) => { const m = M(); const rows = m ? [...m.querySelectorAll('.syn-ctx-route')] : []; const row = rows[i]; if (! row) return 'no row';
    const v = row.querySelector('.ctl-val'), r = v.getBoundingClientRect(); const x = r.left + r.width/2, y = r.top + r.height/2;   /* fb564 — the value IS the depth editor: up = more */
    v.dispatchEvent(new PointerEvent('pointerdown', { bubbles:true, cancelable:true, clientX:x, clientY:y, pointerId:1, buttons:1, button:0 }));
    v.dispatchEvent(new PointerEvent('pointermove', { bubbles:true, cancelable:true, clientX:x, clientY:y - dy, pointerId:1, buttons:1 }));
    v.dispatchEvent(new PointerEvent('pointerup',   { bubbles:true, cancelable:true, clientX:x, clientY:y - dy, pointerId:1 }));
    return 'dragged'; };
  window.__rowClick = (i, cls) => { const m = M(); const rows = m ? [...m.querySelectorAll('.syn-ctx-route')] : []; const row = rows[i]; if (! row) return 'no row';
    const g = row.querySelector('.' + cls); if (! g) return 'no glyph'; g.click(); return 'clicked'; };
  window.__allTargets = () => {
    // every element targets() advertises, resolved the way the drag resolves them: the shipped
    // probe surface is __ctlDestAt, which runs destAt() over the SAME list
    const sel = ['#syn-panel .knob[data-syn]', '#syn-panel .coarse-pill .coarse-val[data-coarse]', '#syn-panel .sub-pill .sub-val',
                 '#syn-panel .warp2-pill .warp2-val[data-warp2-amt]', '#syn-panel .ph-val[data-ph]', '#syn-panel .device.filter .fk-em',
                 '#syn-panel [data-mod-dest]', '.ti-card.open [data-mod-dest]', '#syn-panel .flt-back-knobs .knob', '#flt-spread-pill', '#mod-engine .mv-dep'];
    const out = []; const seen = new Set();
    document.querySelectorAll(sel.join(',')).forEach((el) => { if (seen.has(el)) return; seen.add(el);
      const r = el.getBoundingClientRect(); if (! (r.width > 2 && r.height > 2)) return;
      if (r.right < 0 || r.bottom < 0 || r.left > window.innerWidth || r.top > window.innerHeight) return;
      const cs = getComputedStyle(el); if (cs.visibility === 'hidden' || cs.display === 'none') return;   // (pointer-events:none on a wrapper is fine — the ring inside takes the click and it bubbles)
      const g = window.__ctlDestAt(el); if (! g) return;
      out.push({ el, dest:g.dest, kind:g.kind, id:(el.getAttribute('data-syn')||el.getAttribute('data-mod-dest')||el.className||el.tagName)+'' }); });
    window.__tg = out; return out.map(o => ({ dest:o.dest, kind:o.kind, id:o.id })); };
  window.__sweep = () => {
    const bad = []; let n = 0; const panel = document.getElementById('syn-panel').getBoundingClientRect();
    for (const t of window.__tg) {
      window.__mClose(); const s = window.__mOpen(t.el); n++;
      const inside = s.rect && s.rect[0] >= panel.left - 0.5 && s.rect[1] >= panel.top - 0.5 && s.rect[2] <= panel.right + 0.5 && s.rect[3] <= panel.bottom + 0.5;
      const ok = s.act && s.rows.some(r => r.startsWith('Modulate')) && s.rows.some(r => r === 'Reset to default') && ! s.browser && inside
                 && document.querySelectorAll('.syn-ctx-menu.act').length === 1;
      if (! ok) bad.push({ id:t.id, dest:t.dest, act:s.act, rows:s.rows.slice(0,4), browser:s.browser, inside, rect:s.rect });
    }
    window.__mClose(); return { n, bad:bad.slice(0, 12), nbad:bad.length }; };
};

(async () => {
  const P = mutatedPage();
  const b = await puppeteer.launch({ executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless:'new', args:['--no-sandbox','--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width:VW, height:VH, deviceScaleFactor:2 });
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 160)));
  const choice = {}; ['A','B','C','D'].forEach(o => { choice['SYN_OSC_' + o + '_WARP_MODE'] = WARP_N; choice['SYN_OSC_' + o + '_WARP2_MODE'] = WARP_N; choice['SYN_OSC_' + o + '_ENGINE'] = 7; });
  await pg.evaluateOnNewDocument(STUB, { choice }); await pg.evaluateOnNewDocument(HELPERS);
  await pg.goto('file://' + P, { waitUntil:'load', timeout:60000 }); await new Promise(r => setTimeout(r, 2400));
  await pg.evaluate(() => { document.documentElement.classList.remove('card-only-late');
    document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot'));
    const sp = document.getElementById('syn-panel'); sp.classList.remove('hidden'); sp.style.display = 'block';
    try { document.getElementById('syn-btn').click(); } catch(e){} dispatchEvent(new Event('resize')); });
  await new Promise(r => setTimeout(r, 900));
  // a rack Filter device and a docked GLITCH card, so the rack and the cards are in the sweep too
  await pg.evaluate(async () => { try { window.__fxAdd('flt'); } catch(e){} try { window.__fxAdd('reverb'); } catch(e){}
    try { window.__openFlowCard('glitch'); } catch(e){} await new Promise(r => setTimeout(r, 900));
    const card = document.querySelector('.ti-card.gli-ext'); if (card) { card.style.left = '4px'; card.style.top = '4px'; }
    // flip oscillator A to its BACK face so the pills (coarse · sub · warp 2 · phase · blend) are on screen for the sweep
    const oa = document.getElementById('osc-a-device'); if (oa) oa.classList.add('swapped'); });
  await new Promise(r => setTimeout(r, 600));

  console.log('\n══ fb563 — ONE RIGHT-CLICK MENU ON EVERY CONTROL ══');
  console.log('   page ' + P + '   viewport ' + VW + '×' + VH + (MUT ? '   MUTATION ' + MUT : '') + '\n');

  // ── 1 · ONE MENU EVERYWHERE ─────────────────────────────────────────────────────────────
  const tg = await pg.evaluate(() => window.__allTargets());
  const sw = await pg.evaluate(() => window.__sweep());
  const kinds = {}; tg.forEach(t => { kinds[t.kind] = (kinds[t.kind]||0) + 1; });
  const fam = { knob: tg.some(t => /^SYN_OSC_[A-D]_/.test(t.id)), pill: tg.some(t => (t.dest >= 76 && t.dest < 92) || (t.dest >= 42 && t.dest <= 53) || (t.dest >= 300 && t.dest <= 303) || (t.dest >= 1870 && t.dest <= 1877)), cut: tg.some(t => t.kind === 'cut'),
                fslot: tg.some(t => t.kind === 'fslot'), rack: tg.some(t => t.dest >= 694 && t.dest < 1846),
                card: tg.some(t => t.id.indexOf('cell') >= 0 || (t.dest >= 23 && t.dest <= 25) || (t.dest >= 434 && t.dest <= 471)) };
  chk(tg.length >= 20 && Object.keys(fam).every(k => fam[k]), '1  the sweep covers every family: synth knob · blend pill · cut emblem · filter slot · rack knob · card cell',
      tg.length + ' destinations on screen  ' + JSON.stringify(kinds) + '  ' + JSON.stringify(fam));
  chk(sw.nbad === 0, '1  every destination opens exactly one house menu, inside the panel, with Modulate + Reset', sw.n + ' opened, ' + sw.nbad + ' wrong' + (sw.nbad ? '\n         ' + JSON.stringify(sw.bad) : ''));

  // ── 2 · THE PICKER ROUTES ───────────────────────────────────────────────────────────────
  const WARP = '#syn-panel .knob[data-syn="SYN_OSC_A_WARP_AMOUNT"]';
  await pg.evaluate(() => window.__clearRoutes());
  let s = await pg.evaluate((sel) => { window.__mClose(); return window.__mOpen(document.querySelector(sel)); }, WARP);
  chk(s.act && s.head === '' && /\bctl\b/.test(s.cls) && s.seps === 0, '2  Warp A opens its menu — no header, no separators, the grid (fb564)', JSON.stringify({ head:s.head, cls:s.cls, seps:s.seps, rows:s.rows }));
  let r = await pg.evaluate(() => { const ok = window.__mRow('Modulate'); return { ok, cats: window.__bCats(), menu: window.__mState().act }; });
  chk(r.ok && r.cats && r.cats.join('|') === 'All|Envelopes|LFOs|Macros|Keys|Audio|MIDI' && ! r.menu, '2  Modulate › opens the browser: All · Envelopes · LFOs · Macros · Keys · Audio · MIDI, the menu steps aside', JSON.stringify(r.cats));
  r = await pg.evaluate(() => { const a = window.__bPick('LFOs', 'LFO 2'); return { a, routes: window.__routes(), open: !!document.querySelector('.tpb-panel') }; });
  chk(r.a === 'dot' && r.open && r.routes.length === 1 && r.routes[0].s === 'lfo2' && r.routes[0].d === 3 && Math.abs(r.routes[0].v - 0.5) < 1e-6,
      '2  LFO 2 → route {lfo2 → 3, 0.5}, the dot lit, the browser still open', JSON.stringify(r));
  r = await pg.evaluate(() => { const a = window.__bPick('LFOs', 'LFO 2'); return { a, routes: window.__routes() }; });
  chk(r.a === 'nodot' && r.routes.length === 0, '2  LFO 2 again → the route is gone, the dot is off', JSON.stringify(r));
  r = await pg.evaluate(() => { const a = window.__bPick('Envelopes', 'Env 1'); return { a, routes: window.__routes() }; });
  chk(r.a === 'dot' && r.routes.length === 1 && r.routes[0].s === 'env1' && Math.abs(r.routes[0].v - 1.0) < 1e-6, '2  Env 1 → {env1 → 3, 1.0}', JSON.stringify(r.routes));
  r = await pg.evaluate(() => { const a = window.__bSearch('filter 2', 'Filter 2'); return { a, routes: window.__routes() }; });
  chk(r.a === 'picked' && r.routes.some(x => x.s === 'fol7' && x.d === 3), '2  search "filter 2" → Filter 2 routes as the seventh follower (215/216 were unreadable before)', JSON.stringify(r.routes));
  const envInAll = await pg.evaluate(() => { const s = document.querySelector('.tpb-srch'); s.value = ''; s.oninput(); window.__bPick('All', 'Env 1'); return window.__bRows(window.__bPanes()[1]).filter(x => x.dot).map(x => x.name); });
  chk(envInAll.indexOf('Filter 2') >= 0 && envInAll.indexOf('Env 1') < 0, '2  All mirrors the state: a toggled-off Env 1 loses its dot, Filter 2 keeps it', JSON.stringify(envInAll));
  // fb563 Phase 2 — the six new families route through the same picker, at the same defaults
  r = await pg.evaluate(() => { const a = window.__bPick('Macros', 'Macro 3'), b = window.__bPick('MIDI', 'Mod Wheel'), c = window.__bPick('Keys', 'Random'), d = window.__bPick('Keys', 'Alt'), e = window.__bPick('MIDI', 'Pitch Bend'), f = window.__bPick('MIDI', 'Aftertouch');
    return { a, b, c, d, e, f, routes: window.__routes().filter(x => /^(mac|whl|rnd|alt|bend|at)/.test(x.s)).map(x => x.s + '@' + x.v).sort().join(',') }; });
  chk(r.a === 'dot' && r.b === 'dot' && r.c === 'dot' && r.d === 'dot' && r.e === 'dot' && r.f === 'dot' && r.routes === 'alt@1,at@1,bend@1,mac3@1,rnd1@1,whl@1',
      '2  Macro 3 · Mod Wheel · Random · Alt · Pitch Bend · Aftertouch all route at full depth', r.routes);
  r = await pg.evaluate(() => { window.__mClose(); const st = window.__mOpen(document.querySelector('#syn-panel .knob[data-syn="SYN_OSC_A_WARP_AMOUNT"]')); return st.routes; });
  chk(r.some(x => x === 'Macro 3 · 100%') && r.some(x => x === 'Mod Wheel · 100%') && r.some(x => x === 'Random · 100%') && r.some(x => x === 'Alt · 100%') && r.some(x => x === 'Pitch Bend · 100%') && r.some(x => x === 'Aftertouch · 100%'),
      '2  and the menu names each of them on its own row', JSON.stringify(r));

  // ── 3 · THE ROUTE ROW ───────────────────────────────────────────────────────────────────
  await pg.evaluate(() => { window.__mClose(); window.__clearRoutes(); window.__tiAddRoute(1, 0, 3); window.__tiAddRoute(0, 2, 3); });
  s = await pg.evaluate((sel) => window.__mOpen(document.querySelector(sel)), WARP);
  chk(s.routes.length === 2 && s.routes[0] === 'Env 1 · 100%' && s.routes[1] === 'LFO 2 · 50%' && s.rows.some(x => x === 'Remove all modulators'),
      '3  two connections → two rows with their depths, and Remove all modulators', JSON.stringify(s.routes));
  r = await pg.evaluate(() => { const a = window.__valDrag(1, 30); return { a, routes: window.__routes(), row: window.__mState().routes[1] }; });
  chk(r.routes[1] && Math.abs(r.routes[1].v - 0.8) < 0.015 && /LFO 2 · 80%/.test(r.row), '3  drag the LFO 2 value up 30 px → depth +80% (1 px = 1 %, signed)', JSON.stringify({ v:r.routes[1] && r.routes[1].v, row:r.row }));
  r = await pg.evaluate(() => { const a = window.__valDrag(0, -50); return { a, routes: window.__routes(), row: window.__mState().routes[0] }; });
  chk(r.routes[0] && Math.abs(r.routes[0].v - 0.5) < 0.015 && /Env 1 · 50%/.test(r.row), '3  drag the Env 1 value down 50 px → depth 50% (a magnitude, floors at 0)', JSON.stringify({ v:r.routes[0] && r.routes[0].v, row:r.row }));
  r = await pg.evaluate(() => { const a = window.__valDrag(0, -200); const v1 = window.__routes()[0].v; const row = document.querySelector('#syn-ctx-menu .syn-ctx-route .ctl-val');
    row.dispatchEvent(new MouseEvent('dblclick', { bubbles:true, cancelable:true, view:window })); return { a, v1, v2: window.__routes()[0].v, row: window.__mState().routes[0] }; });
  chk(r.v1 === 0 && r.v2 === 1 && r.row === 'Env 1 · 100%', '3  an over-drag floors at 0 %; double-click the value → the drop default (100 %)', JSON.stringify(r));
  r = await pg.evaluate(() => { const a = window.__rowClick(0, 'ctl-crv'); return { a, host: window.__crvHostKey ? window.__crvHostKey() : null, menu: window.__mState().act }; });
  chk(r.a === 'clicked' && r.host === 'mod' && ! r.menu, '3  ⤢ opens the house curve editor on the connection (host = mod), the menu steps aside', JSON.stringify(r));
  await pg.evaluate(() => { try { window.__crvClose(); } catch(e){} });
  s = await pg.evaluate((sel) => { window.__mClose(); return window.__mOpen(document.querySelector(sel)); }, WARP);
  r = await pg.evaluate(() => { const a = window.__rowClick(1, 'ctl-rx'); const st = window.__mState(); return { a, routes: window.__routes(), st }; });
  chk(r.a === 'clicked' && r.routes.length === 1 && r.routes[0].s === 'env1' && r.st.act && r.st.routes.length === 1, '3  ✕ on LFO 2 removes it; the menu rebuilds in place with one row', JSON.stringify({ routes:r.routes, rows:r.st.routes }));

  // ── 4 · CONTROL ROWS FOLD IN ────────────────────────────────────────────────────────────
  s = await pg.evaluate((sel) => { window.__mClose(); return window.__mOpen(document.querySelector(sel)); }, WARP);
  const wm = s.rows.find(x => x.startsWith('Warp mode'));
  const wmArrow = await pg.evaluate(() => [...document.querySelectorAll('#syn-ctx-menu .syn-ctx-item')].filter(d => d.textContent.trim().startsWith('Warp mode') && d.querySelector('.syn-ctx-arrow')).length);
  chk(!!wm && wm.length > 'Warp mode'.length && wmArrow === 1, '4  Warp A carries "Warp mode · <current>" with the chevron box (fb564: an SVG on the right rail, not a character)', JSON.stringify({ wm, wmArrow }));
  r = await pg.evaluate(() => { const ok = window.__mRow('Warp mode'); const cats = window.__bCats(); const menu = window.__mState().act;
    window.__emits.length = 0; const p = window.__bPick('All', 'Bend'); const em = window.__emits.filter(e => e.name === 'SYN_OSC_A_WARP_MODE'); return { ok, cats, menu, p, em }; });
  if (! (r.cats && r.cats.length)) r.guard = await pg.evaluate(() => { try { return window.__warpGuard({ quiet:true }); } catch(e) { return String(e); } });
  chk(r.ok && r.cats && r.cats[0] === 'All' && r.cats.length > 3 && ! r.menu && r.em.length === 1, '4  that row opens the warp browser (families + All) and picking Bend writes SYN_OSC_A_WARP_MODE', JSON.stringify({ cats:r.cats && r.cats.slice(0,4), em:r.em, guard:r.guard }));
  const FOLD = '#syn-panel .knob[data-syn="SYN_OSC_A_FOLD_AMT"]';
  s = await pg.evaluate((sel) => { window.__mClose(); return window.__mOpen(document.querySelector(sel)); }, FOLD);
  chk(s.act && s.rows.some(x => x.startsWith('Fold shape')), '4  Fold A carries "Fold shape"', JSON.stringify(s.rows));
  r = await pg.evaluate(() => { const ok = window.__mRow('Fold shape'); const st = window.__mState(); return { ok, st }; });
  chk(r.ok && r.st.act && r.st.rows.length >= 3 && ! r.st.rows.some(x => x.startsWith('Modulate')), '4  the Fold shape row opens the flat shape list in place', JSON.stringify(r.st.rows.slice(0, 5)));
  s = await pg.evaluate(() => { window.__mClose(); const k = document.querySelectorAll('#syn-panel .device.filter .flt-back-knobs .knob')[1]; return window.__mOpen(k); });
  chk(s.act && s.rows.some(x => x.startsWith('Drive type')) && s.rows.some(x => x.startsWith('Modulate')), '4  the filter\'s back Drive knob: Modulate + "Drive type"', JSON.stringify(s.rows));
  s = await pg.evaluate(() => { window.__mClose(); const p = document.querySelector('#syn-panel .device.osc .blend-pill[data-mod-dest]'); return window.__mOpen(p); });
  chk(s.act && s.rows.some(x => x.startsWith('Blend mode')) && s.rows.some(x => x.startsWith('Modulate')), '4  a blend pill: Modulate + "Blend mode"', JSON.stringify(s.rows));
  s = await pg.evaluate(() => { window.__mClose();
    const D = window.__fxrDevs(); const i = D.findIndex(x => x && x.core === 'flt'); const card = document.querySelectorAll('#syn-panel .fxr-dev')[i]; if (! card) return { act:false, rows:['no flt card'] };
    const kn = [...card.querySelectorAll('.fxr-knob')].find(k => /Drive/i.test((k.querySelector('.fxr-lab,.fxr-kl')||{}).textContent||''));
    if (! kn) return { act:false, rows:['no Drive knob among ' + [...card.querySelectorAll('.fxr-knob')].map(k => (k.querySelector('.fxr-lab,.fxr-kl')||{}).textContent).join('|')] };
    const clip = document.querySelector('.fxr-clip'); clip.scrollLeft = card.offsetLeft - clip.offsetLeft - 8;
    return window.__mOpen(kn); });
  chk(s.act && s.rows.some(x => x.startsWith('Drive type')) && s.rows.some(x => x.startsWith('Modulate')), '4  the rack Filter\'s Drive knob: Modulate + "Drive type"', JSON.stringify(s.rows));
  // a control that is NOT a destination but registers rows still gets the one menu (minus the
  // modulation half). No shipped control is in that position today (the KNOB_MENUS Keytrack entry
  // never matched an element — its target is the native <select>), so this registers a row on a
  // [data-syn] <select> and right-clicks it: the fallback door, driven for real.
  s = await pg.evaluate(() => { window.__mClose(); const k = document.querySelector('#syn-panel select[data-syn]'); if (! k) return { act:false, rows:['no [data-syn] select'] };
    window.__ctlMenuRows.on(k, () => [{ label:'Gate row', badge:'x  ›', keepOpen:true, onPick(){} }]); const st = window.__mOpen(k); st.dest = window.__ctlDestAt(k); return st; });
  chk(s.act && s.dest === null && s.rows.some(x => x.startsWith('Gate row')) && ! s.rows.some(x => x.startsWith('Modulate')) && s.rows.some(x => x === 'Reset to default'),
      '4  a non-destination control with registered rows still gets the one menu — its rows + Reset, no Modulate', JSON.stringify({ dest:s.dest, rows:s.rows }));

  // ── 5 · RESET ───────────────────────────────────────────────────────────────────────────
  r = await pg.evaluate((sel) => { window.__mClose(); window.__stubState('SYN_OSC_A_WARP_AMOUNT').setNormalisedValue(0.8); window.__emits.length = 0;
    window.__mOpen(document.querySelector(sel)); const ok = window.__mRow('Reset to default'); return { ok, em: window.__emits.filter(e => e.name === 'SYN_OSC_A_WARP_AMOUNT'), now: window.__stubState('SYN_OSC_A_WARP_AMOUNT').getNormalisedValue() }; }, WARP);
  chk(r.ok && r.em.length >= 1 && r.now === 0, '5  Reset to default on Warp A (at 0.8) writes its default 0', JSON.stringify(r));
  r = await pg.evaluate(() => { window.__mClose();
    const D = window.__fxrDevs(); const i = D.findIndex(x => x && x.core === 'reverb'); const card = document.querySelectorAll('#syn-panel .fxr-dev')[i]; if (! card) return { err:'no reverb card' };
    const kn = card.querySelector('.fxr-knob[data-mod-dest]'); const dial = kn.querySelector('.fxr-dial'); const kA = +dial.getAttribute('data-k');
    D[i].knobs[kA].v = 83; const clip = document.querySelector('.fxr-clip'); clip.scrollLeft = card.offsetLeft - clip.offsetLeft - 8;
    window.__mOpen(kn); const ok = window.__mRow('Reset to default'); return { ok, v: D[i].knobs[kA].v, kA }; });
  chk(r.ok && r.v !== 83 && typeof r.v === 'number', '5  Reset to default on a rack dial (set to 83) restores its template default', JSON.stringify(r));

  // ── 6 · COPY / PASTE ────────────────────────────────────────────────────────────────────
  r = await pg.evaluate((w, f) => { window.__mClose(); window.__clearRoutes(); window.__tiAddRoute(1, 0, 3); window.__tiAddRoute(0, 3, 3);
    const rs = window.__routes(); rs.forEach(x => {}); window.__mOpen(document.querySelector(w)); const c = window.__mRow('Copy modulators'); const n = window.__ctlClip();
    window.__mClose(); window.__mOpen(document.querySelector(f)); const p = window.__mRow('Paste modulators'); return { c, n, p, routes: window.__routes() }; }, WARP, FOLD);
  const onFold = r.routes.filter(x => x.d === 4).map(x => x.s + '@' + x.v).sort().join(',');
  chk(r.c && r.n === 2 && r.p && onFold === 'env1@1,lfo3@0.5', '6  Copy on Warp A, Paste on Fold A → the same two connections, same depths', JSON.stringify({ n:r.n, onFold }));

  // ── 7 · THE CODEC ───────────────────────────────────────────────────────────────────────
  r = await pg.evaluate(() => { const W = window.__modWire; const d = (n) => JSON.stringify(W.decode(n));
    let ident = true; for (const n of [0,1,9,100,101,131,200,201,210,211,214,215,216,220,227,228,230,231,232,240,243,244]) { const s = W.decode(n); if (! s || W.encode(s) !== n) ident = false; }
    return { f6:d(215), f7:d(216), x217:d(217), key:d(201), vel:d(200), e32:d(131), x132:d(132), x10:d(10),
             m1:d(220), m8:d(227), m9:d(228), x229:d(229), whl:d(230), at:d(231), bend:d(232), r1:d(240), r4:d(243), x245:d(245), alt:d(244), ident }; });
  chk(r.f6 === '{"fol":6}' && r.f7 === '{"fol":7}' && r.x217 === 'null' && r.key === '{"note":1}' && r.vel === '{"vel":1}' && r.e32 === '{"env":32}' && r.x132 === 'null' && r.x10 === 'null'
      && r.m1 === '{"mac":1}' && r.m8 === '{"mac":8}' && r.m9 === '{"mac":9}' && r.x229 === 'null' && r.whl === '{"whl":1}' && r.at === '{"at":1}' && r.bend === '{"bend":1}' && r.r1 === '{"rnd":1}' && r.r4 === '{"rnd":4}' && r.x245 === 'null' && r.alt === '{"alt":1}' && r.ident,
      '7  decode: followers 215/216, macros 220..228 (nine — fb565), wheel 230, aftertouch 231, bend 232, random 240..243, alt 244; 217/229/245/132/10 rejected; encode∘decode = identity', JSON.stringify(r));
  // ── 10 · BYPASS + SCALE BY (Phase 3) — the route row's own right-click ─────────────────────
  r = await pg.evaluate(() => { window.__mClose(); window.__clearRoutes(); window.__tiAddRoute(1, 0, 3);
    const oa = document.getElementById('osc-a-device'); if (oa) oa.classList.remove('swapped');
    window.__mOpen(document.querySelector('#syn-panel .knob[data-syn="SYN_OSC_A_WARP_AMOUNT"]'));
    const row = document.querySelector('#syn-ctx-menu .syn-ctx-route'); if (! row) return { err:'no row' };
    const q = row.getBoundingClientRect();
    row.dispatchEvent(new MouseEvent('contextmenu', { bubbles:true, cancelable:true, clientX:q.left + 20, clientY:q.top + 6, view:window }));
    const st = window.__mState(); return { head:st.head, rows:st.rows }; });
  chk(r.rows && r.rows.some(x => x === 'Bypass') && r.rows.some(x => x.startsWith('Scale by')) && r.rows.some(x => x === 'Remove') && r.head === '',
      '10 right-click a route row → Bypass · Scale by › · Remove (no title — the fb564 grid)', JSON.stringify(r));
  r = await pg.evaluate(() => { window.__natives.length = 0; const ok = window.__mRow('Bypass'); const st = window.__mState();
    const pushed = window.__natives.filter(n => n.fn === 'setSynthMod').map(n => n.args[0]).pop() || '';
    const nm = document.querySelector('#syn-ctx-menu .syn-ctx-route .ctl-name');
    return { ok, routes: window.__routes(), routesTxt: st.routes, pushed: /"b":1/.test(pushed), dim: nm ? getComputedStyle(nm).opacity : null }; });
  chk(r.ok && r.routes[0] && r.routes[0].b === 1 && r.routes[0].s === 'env1' && r.pushed && r.routesTxt[0] === 'Env 1 · off' && r.dim === '0.45',
      '10 Bypass keeps the connection, marks it b:1 on the wire, the row reads "off" and dims', JSON.stringify({ b:r.routes[0] && r.routes[0].b, pushed:r.pushed, txt:r.routesTxt, dim:r.dim }));
  r = await pg.evaluate(() => { const row = document.querySelector('#syn-ctx-menu .syn-ctx-route'); const q = row.getBoundingClientRect();
    row.dispatchEvent(new MouseEvent('contextmenu', { bubbles:true, cancelable:true, clientX:q.left + 20, clientY:q.top + 6, view:window }));
    const a = window.__mRow('Bypass'); const rt = window.__routes(); return { a, b: rt[0] && rt[0].b }; });
  chk(r.a && r.b === 0, '10 Bypass again → un-bypassed', JSON.stringify(r));
  r = await pg.evaluate(() => { const row = document.querySelector('#syn-ctx-menu .syn-ctx-route'); const q = row.getBoundingClientRect();
    row.dispatchEvent(new MouseEvent('contextmenu', { bubbles:true, cancelable:true, clientX:q.left + 20, clientY:q.top + 6, view:window }));
    const ok = window.__mRow('Scale by'); const cats = window.__bCats(); window.__natives.length = 0; const p = window.__bPick('Macros', 'Macro 2');
    const pushed = window.__natives.filter(n => n.fn === 'setSynthMod').map(n => n.args[0]).pop() || '';
    try { window.__tpbClose(); } catch(e){}
    window.__mOpen(document.querySelector('#syn-panel .knob[data-syn="SYN_OSC_A_WARP_AMOUNT"]')); const st = window.__mState();
    return { ok, cats, p, x: (window.__routes()[0] || {}).x, pushed: /"x":221/.test(pushed), txt: st.routes }; });
  chk(r.ok && r.cats && r.cats.length === 7 && r.p === 'dot' && r.x === 'mac2' && r.pushed && r.txt[0] === 'Env 1 × Macro 2 · 100%',
      '10 Scale by › opens the families, Macro 2 becomes the aux (x:221 on the wire), the row reads "Env 1 × Macro 2"', JSON.stringify({ cats:r.cats, x:r.x, pushed:r.pushed, txt:r.txt }));
  r = await pg.evaluate(() => { const row = document.querySelector('#syn-ctx-menu .syn-ctx-route'); const q = row.getBoundingClientRect();
    row.dispatchEvent(new MouseEvent('contextmenu', { bubbles:true, cancelable:true, clientX:q.left + 20, clientY:q.top + 6, view:window }));
    const ok = window.__mRow('No scaling'); return { ok, x: (window.__routes()[0] || {}).x, txt: window.__mState().routes }; });
  chk(r.ok && r.x === null && r.txt[0] === 'Env 1 · 100%', '10 No scaling → the aux is gone', JSON.stringify(r));

  // ── 11 · MIDI LEARN (Phase 4) — the row, the native calls, the pushed map ──────────────────
  r = await pg.evaluate(() => { window.__mClose(); window.__natives.length = 0; window.__midiMap = {};
    const oa = document.getElementById('osc-a-device'); if (oa) oa.classList.remove('swapped');
    const st = window.__mOpen(document.querySelector('#syn-panel .knob[data-syn="SYN_OSC_A_WARP_AMOUNT"]'));
    const has = st.rows.some(x => x === 'MIDI Learn'); const ok = window.__mRow('MIDI Learn');
    const calls = window.__natives.filter(n => n.fn === 'setMidiLearn').map(n => n.args[0]);
    const st2 = window.__mOpen(document.querySelector('#syn-panel .knob[data-syn="SYN_OSC_A_WARP_AMOUNT"]'));
    return { has, ok, calls, waiting: st2.rows.some(x => x.startsWith('MIDI Learn') && /waiting/.test(x)), learning: window.__midiLearnFor() }; });
  chk(r.has && r.ok && r.calls.length === 1 && r.calls[0] === 'SYN_OSC_A_WARP_AMOUNT' && r.waiting && r.learning === 'SYN_OSC_A_WARP_AMOUNT',
      '11 "MIDI Learn" on Warp A arms the parameter (setMidiLearn) and the row reads "waiting…"', JSON.stringify(r));
  r = await pg.evaluate(() => { // the processor pushes the new map once the CC lands — simulate that push
    window.__midiMap = { "74": "SYN_OSC_A_WARP_AMOUNT" }; window.__midiLearnedCc = 74; window.__midiMapChanged();
    const st = window.__mState(); const row = st.rows.find(x => x.startsWith('MIDI CC 74'));
    window.__natives.length = 0; const ok = window.__mRow('MIDI CC 74');
    const rm = window.__natives.filter(n => n.fn === 'removeMidiCc').map(n => n.args[0]);
    const st3 = window.__mOpen(document.querySelector('#syn-panel .knob[data-syn="SYN_OSC_A_WARP_AMOUNT"]'));
    return { rebuilt: st.act, row, ok, rm, back: st3.rows.some(x => x === 'MIDI Learn'), learning: window.__midiLearnFor() }; });
  chk(r.rebuilt && r.row && /Remove$/.test(r.row) && r.ok && r.rm.length === 1 && r.rm[0] === 'SYN_OSC_A_WARP_AMOUNT' && r.back && r.learning === null,
      '11 the pushed map rebuilds the open menu into "MIDI CC 74 · Remove"; Remove calls removeMidiCc and the row is "MIDI Learn" again', JSON.stringify(r));
  r = await pg.evaluate(() => { window.__mClose();
    const D = window.__fxrDevs(); const i = D.findIndex(x => x && x.core === 'reverb'); const card = document.querySelectorAll('#syn-panel .fxr-dev')[i];
    const kn = card.querySelector('.fxr-knob[data-mod-dest]'); const clip = document.querySelector('.fxr-clip'); clip.scrollLeft = card.offsetLeft - clip.offsetLeft - 8;
    const st = window.__mOpen(kn); window.__natives.length = 0; window.__mRow('MIDI Learn'); const calls = window.__natives.filter(n => n.fn === 'setMidiLearn').map(n => n.args[0]);
    window.__mClose(); return { has: st.rows.some(x => x === 'MIDI Learn'), calls }; });
  chk(r.has && r.calls.length === 1 && /^SYN_RVB_/.test(r.calls[0]), '11 a rack knob offers MIDI Learn on its own parameter id', JSON.stringify(r));

  // ── 9 · THE MACROS VIEW (Phase 2) ───────────────────────────────────────────────────────
  r = await pg.evaluate(async () => { window.__mClose(); window.__clearRoutes();
    const btn = document.querySelector('#syn-panel .vm-macros-btn'); if (! btn) return { err:'no Macros button' }; btn.click();
    const cells = [...document.querySelectorAll('#syn-panel .vm-macro')]; const vis = cells.filter(c => c.getBoundingClientRect().width > 2).length;
    window.__natives.length = 0; window.__macroSet(2, 63); const w = window.__natives.filter(n => n.fn === 'setSynParam' && n.args[0] === 'SYN_MACRO_3');
    // a REAL drag on the macro's name onto Warp A (the drag grip, not the picker). The docked GLITCH
    // card sits over Warp A (fb524b: an open card is opaque to drops), so it is closed first.
    document.querySelectorAll('.ti-card.open').forEach(c => c.classList.remove('open'));
    const oa = document.getElementById('osc-a-device'); if (oa) oa.classList.remove('swapped');   // the sweep flipped osc A to its back face; Warp A lives on the front
    const lab = document.querySelector('#syn-panel .vm-macro[data-macro="5"] .vm-ml'), knob = document.querySelector('#syn-panel .knob[data-syn="SYN_OSC_A_WARP_AMOUNT"]');
    const lr = lab.getBoundingClientRect(), kr = knob.getBoundingClientRect();
    const ev = (t, type, x, y) => t.dispatchEvent(new PointerEvent(type, { bubbles:true, cancelable:true, clientX:x, clientY:y, pointerId:1, buttons:1, screenX:x, screenY:y }));
    ev(lab, 'pointerdown', lr.left + 4, lr.top + 4);
    ev(document, 'pointermove', lr.left + 20, lr.top + 20); ev(document, 'pointermove', kr.left + kr.width/2, kr.top + kr.height/2);
    ev(document, 'pointerup', kr.left + kr.width/2, kr.top + kr.height/2);
    return { n: cells.length, vis, v3: window.__macroVal(2), writes: w.length, norm: w.length ? +w[0].args[1] : null, routes: window.__routes(), bend: window.__bendRange() }; });
  chk(r.n === 9 && r.vis === 9, '9  the Macros view shows nine real macros, three rows of three (fb565)', JSON.stringify({ n:r.n, vis:r.vis }));
  chk(r.v3 === 63 && r.writes === 1 && Math.abs(r.norm - 0.63) < 1e-6, '9  a macro writes SYN_MACRO_n through setSynParam (63 → 0.63)', JSON.stringify({ v3:r.v3, writes:r.writes, norm:r.norm }));
  chk(r.routes.length === 1 && r.routes[0].s === 'mac5' && r.routes[0].d === 3 && r.routes[0].v === 1, '9  dragging a macro\'s NAME onto Warp A makes the route {mac5 → 3, 1.0}', JSON.stringify(r.routes));
  chk(r.bend === 2, '9  the Bend row reads its default range, 2 semitones', JSON.stringify(r.bend));

  // ── 12 · THE GRID (fb564) ───────────────────────────────────────────────────────────────
  // ⚠️ bar 9 ended with a REAL drag; the page's fb178 guard swallows any .syn-ctx-item click
  // within 300 ms of a drag's end (a drop must never double as a click). A mouse cannot get
  // here that fast; this gate can, so it waits like a hand would.
  await new Promise(r => setTimeout(r, 350));
  r = await pg.evaluate((sel) => { window.__mClose(); window.__clearRoutes(); window.__tiAddRoute(1, 0, 3); window.__tiAddRoute(0, 2, 3);
    const oa = document.getElementById('osc-a-device'); if (oa) oa.classList.remove('swapped');
    const st = window.__mOpen(document.querySelector(sel)); const m = document.getElementById('syn-ctx-menu');
    const R = (el) => el.getBoundingClientRect();
    const labels = [...m.querySelectorAll('.syn-ctx-lab, .ctl-name')].map(e => +R(e).left.toFixed(1));
    const vals = [...m.querySelectorAll('.ctl-val')].map(e => +R(e).right.toFixed(1));
    const rail = [...m.querySelectorAll('.ctl-rx, .syn-ctx-arrow')].map(e => ({ r:+R(e).right.toFixed(1), w:+R(e).width.toFixed(1), h:+R(e).height.toFixed(1) }));
    const crv = [...m.querySelectorAll('.ctl-crv')].map(e => ({ r:+R(e).right.toFixed(1), w:+R(e).width.toFixed(1), h:+R(e).height.toFixed(1) }));
    const rows = [...m.querySelectorAll('.syn-ctx-item, .syn-ctx-route')].map(e => +R(e).height.toFixed(1));
    const uniq = (a) => [...new Set(a)];
    return { head:st.head, seps:st.seps, labels:uniq(labels), vals:uniq(vals), rail:uniq(rail.map(x => x.r)), boxes:uniq(rail.concat(crv).map(x => x.w + 'x' + x.h)), crv:uniq(crv.map(x => x.r)), rows:uniq(rows), n:labels.length, nRail:rail.length, nCrv:crv.length }; }, WARP);
  chk(r.head === '' && r.seps === 0 && r.n >= 7 && r.labels.length === 1 && r.vals.length === 1 && r.nRail >= 4 && r.rail.length === 1 && r.nCrv === 2 && r.crv.length === 1
      && r.boxes.length === 1 && r.boxes[0] === '16x16' && r.rows.length === 1 && r.rows[0] === 24,
      '12 the grid: no header, no rules, one left rail for every word, one right edge for every value, ⤢ · ✕ · › identical 16 px boxes on one right rail, 24 px rows', JSON.stringify(r));
  // the hover colours: ✕ goes RED, ⤢ goes white (Max: "whenever we hover over the X button, it's red")
  r = await pg.evaluate(() => { const x = document.querySelector('#syn-ctx-menu .ctl-rx'), c = document.querySelector('#syn-ctx-menu .ctl-crv');
    const st = document.createElement('style'); st.textContent = '#syn-panel .syn-ctx-menu.ctl .syn-ctx-route .ctl-rx.__hov { color:#EF4444; }'; document.head.appendChild(st);   // :hover cannot be forced from script; the rule under test is read from the sheet instead
    const rules = [...document.styleSheets].flatMap(s => { try { return [...s.cssRules]; } catch(e) { return []; } }).map(r => r.cssText || '');
    const red = rules.some(t => /\.ctl-rx:hover/.test(t) && /239, 68, 68|#ef4444/i.test(t)), white = rules.some(t => /\.ctl-crv:hover/.test(t) && /255, 255, 255|#fff/i.test(t));
    st.remove(); return { red, white, rest: getComputedStyle(x).color }; });
  chk(r.red && r.white && r.rest === 'rgba(255, 255, 255, 0.45)', '12 ✕ rests at .45 white and turns red on hover; ⤢ turns white', JSON.stringify(r));
  // a click on the row opens its options — no second right-click needed
  r = await pg.evaluate(() => { const row = document.querySelector('#syn-ctx-menu .syn-ctx-route'); const q = row.getBoundingClientRect();
    row.dispatchEvent(new MouseEvent('click', { bubbles:true, cancelable:true, clientX:q.left + 30, clientY:q.top + 12, view:window })); const st = window.__mState(); return { rows:st.rows, head:st.head, cls:st.cls }; });
  chk(r.rows.length >= 3 && r.rows[0] === 'Bypass' && r.rows[1].startsWith('Scale by') && r.rows.some(x => x === 'Remove') && r.head === '' && /\bctl\b/.test(r.cls),
      '12 a click on a route row opens Bypass · Scale by › · Remove, in the same grid', JSON.stringify(r));
  // a DOUBLE-CLICK on a picker source assigns it and closes (the second click of the pair never toggles it off)
  r = await pg.evaluate((sel) => { window.__mClose(); window.__clearRoutes(); window.__mOpen(document.querySelector(sel)); window.__mRow('Modulate');
    const p = window.__bPanes(); window.__bRows(p[0]).find(c => c.name === 'LFOs').el.click();
    const it = () => { const p = window.__bPanes(); if (p.length < 2) return null; return window.__bRows(p[1]).find(x => x.name === 'LFO 4') || null; };   // null once the picker has closed
    const dbl = (el) => { el.dispatchEvent(new MouseEvent('click', { bubbles:true, cancelable:true, detail:1, view:window }));
                          const el2 = it() ? it().el : el;   // the list repaints after a click; the second click lands on the repainted row
                          el2.dispatchEvent(new MouseEvent('click', { bubbles:true, cancelable:true, detail:2, view:window }));
                          const el3 = it() ? it().el : el2; el3.dispatchEvent(new MouseEvent('dblclick', { bubbles:true, cancelable:true, detail:2, view:window })); };
    dbl(it().el); const a = { routes: window.__routes().map(x => x.s), open: !!document.querySelector('.tpb-panel') };
    // and on a source that is ALREADY routed: the double-click keeps it and closes
    window.__mOpen(document.querySelector(sel)); window.__mRow('Modulate'); window.__bRows(window.__bPanes()[0]).find(c => c.name === 'LFOs').el.click();
    dbl(it().el); const b = { routes: window.__routes().map(x => x.s), open: !!document.querySelector('.tpb-panel') };
    return { a, b }; }, WARP);
  chk(r.a.routes.join() === 'lfo4' && ! r.a.open && r.b.routes.join() === 'lfo4' && ! r.b.open,
      '12 double-click LFO 4 in the picker → routed AND the picker closed; double-click it again → still routed, closed', JSON.stringify(r));

  // ── 13 · A MACRO IS A CONTROL (fb564) ───────────────────────────────────────────────────
  r = await pg.evaluate(() => { window.__mClose(); window.__clearRoutes();
    const btn = document.querySelector('#syn-panel .vm-macros-btn'); const vm = document.querySelector('#syn-panel .voice-meta'); if (btn && vm && ! vm.classList.contains('vm-macros-active')) btn.click();
    const mel = document.querySelector('#syn-panel .vm-macro[data-macro="3"]'); const st = window.__mOpen(mel);
    window.__natives.length = 0; const ml = window.__mRow('MIDI Learn'); const learn = window.__natives.filter(n => n.fn === 'setMidiLearn').map(n => n.args[0]);
    const st2 = window.__mOpen(mel); const waiting = st2.rows.some(x => /^MIDI Learn.*waiting/.test(x));
    window.__mRow('MIDI Learn');   // cancels the arm
    return { rows: st.rows, ml, learn, waiting, dest: st.rows.some(x => x.startsWith('Modulate')) }; });
  chk(r.rows.filter(x => x !== 'Paste modulators').join('|') === 'Modulate|Rename|Reset to default|MIDI Learn' && r.dest && r.ml && r.learn.join() === 'SYN_MACRO_3' && r.waiting,
      '13 right-click Macro 3 → Modulate · Rename · Reset to default · MIDI Learn (fb565: a macro is a destination too); MIDI Learn arms SYN_MACRO_3 and reads waiting…', JSON.stringify(r));
  r = await pg.evaluate(() => { window.__mClose(); window.__tiAddRoute(0, 0, 3); try { window.__tiRoutes(); } catch(e){}
    const mel = document.querySelector('#syn-panel .vm-macro[data-macro="3"]'); window.__mOpen(mel); window.__mRow('Rename');
    const inp = document.querySelector('#syn-panel .vm-macro[data-macro="3"] .vm-ml input'); const armed = window.__tiActiveInp === inp;
    if (inp) { inp.value = 'Cutoff'; inp.dispatchEvent(new KeyboardEvent('keydown', { key:'Enter', bubbles:true })); }
    window.__natives.length = 0; window.__macroRename(3, 'Cutoff');
    const saved = window.__natives.filter(n => n.fn === 'setMacroNames').map(n => n.args[0]).pop() || '';
    const label = document.querySelector('#syn-panel .vm-macro[data-macro="3"] .vm-ml').textContent.trim();
    // the route from Macro 3 to Warp A (a REAL pick through the picker) reads by the new name
    const oa = document.getElementById('osc-a-device'); if (oa) oa.classList.remove('swapped');
    window.__mOpen(document.querySelector('#syn-panel .knob[data-syn="SYN_OSC_A_WARP_AMOUNT"]')); window.__mRow('Modulate');
    const pick = window.__bPick('Macros', 'Cutoff'); window.__mClose();
    const st = window.__mOpen(document.querySelector('#syn-panel .knob[data-syn="SYN_OSC_A_WARP_AMOUNT"]')); window.__mClose();
    window.__macroRename(3, '');   // back to the default so later bars read "Macro 3"
    return { armed, saved, label, pick, routes: st.routes, name3: window.__macroName(3) }; });
  chk(r.armed && r.label === 'Cutoff' && /"Cutoff"/.test(r.saved) && r.pick === 'dot' && r.routes.some(x => x === 'Cutoff · 100%') && r.name3 === 'Macro 3',
      '13 Rename → a field (armed for the host-key bridge); "Cutoff" lands on the knob, in the picker, on the route row and in setMacroNames', JSON.stringify(r));

  // ── 15 · A MACRO IS A DESTINATION (fb565) ────────────────────────────────────────────────
  r = await pg.evaluate(() => { window.__mClose(); window.__clearRoutes();
    const MD = window.__MACRO_DEST; const mel = document.querySelector('#syn-panel .vm-macro[data-macro="3"]');
    const g = window.__ctlDestAt(mel); window.__mOpen(mel); window.__mRow('Modulate');
    const cats = window.__bCats(); window.__bRows(window.__bPanes()[0]).find(c => c.name === 'Macros').el.click();
    const macNames = window.__bRows(window.__bPanes()[1]).map(x => x.name);
    const p1 = window.__bPick('LFOs', 'LFO 1'), p2 = window.__bPick('Macros', 'Macro 2'); window.__mClose();
    const routes = window.__routes().map(x => x.s + '→' + x.d + '@' + x.v).sort(); const name = window.__destShortName(MD + 2);
    const st = window.__mOpen(mel); window.__mClose(); const ul = window.__ulTick ? window.__ulTick() : -1;
    return { MD, attr: mel.getAttribute('data-mod-dest'), g, cats: cats && cats.length, macNames, p1, p2, routes, name, rts: st.routes, ul }; });
  chk(r.MD === 1878 && r.attr === '1880' && r.g && r.g.dest === 1880 && r.cats === 7 && r.p1 === 'dot' && r.p2 === 'dot' && r.routes.join() === 'lfo1→1880@0.5,mac2→1880@1'
      && r.name === 'Macro 3' && r.macNames.indexOf('Macro 3') < 0 && r.macNames.indexOf('Macro 2') >= 0 && r.macNames.indexOf('Macro 9') >= 0 && r.rts.join() === 'LFO 1 · 50%,Macro 2 · 100%' && r.ul >= 1,
      '15 Macro 3 is destination 1880: Modulate › LFO 1 and Macro 2 route to it (LFO half, macro full), it is named "Macro 3", its own picker hides Macro 3 and offers Macro 9, its rows read on its menu, it has an underline', JSON.stringify(r));

  // ── 16 · THE HAND-DRIVEN COMET RIDES AT THE FRAME RATE (fb566) ───────────────────────────
  //  The feed pushes the wheel at ≤ 60 Hz and calls __mvP2Tick once per push; between two pushes the
  //  comet's value must move THROUGH the gap, not sit on the last push and jump.
  r = await pg.evaluate(async () => { const sleep = (ms) => new Promise(r => setTimeout(r, ms));
    const push = (w) => { window.__mvWheel = w; window.__mvP2Tick(); };
    push(0.0); await sleep(100); push(0.0); await sleep(100);   // two identical pushes: the clock must NOT count them as motion
    push(0.2); await sleep(100); push(1.0);                       // a 100 ms cadence, then a step from 0.2 to 1.0
    const v0 = window.__tiP2Value({ whl:1 }); await sleep(30); const v1 = window.__tiP2Value({ whl:1 }); await sleep(30); const v2 = window.__tiP2Value({ whl:1 });
    await sleep(320); const v3 = window.__tiP2Value({ whl:1 });   // the clock is stale now: the raw value, never a guess
    return { v0:+v0.toFixed(3), v1:+v1.toFixed(3), v2:+v2.toFixed(3), v3:+v3.toFixed(3) }; });
  chk(r.v0 >= 0.2 && r.v0 < 0.6 && r.v1 > r.v0 && r.v2 > r.v1 && r.v2 < 1.0 && r.v3 === 1.0,
      '16 a wheel push 0.2 → 1.0 on a 100 ms cadence: the comet value climbs THROUGH the gap (interpolated) and lands on 1.0 once the clock is stale', JSON.stringify(r));

  // ── 14 · COPY WITH MODULATORS (fb564) ───────────────────────────────────────────────────
  r = await pg.evaluate(() => { window.__mClose(); window.__clearRoutes(); window.__tiAddRoute(1, 0, 3); window.__tiAddRoute(0, 2, 4); window.__tiAddRoute(0, 5, 673);   // Env 1 → Warp A · LFO 2 → Fold A · LFO 5 → LFO 1 Rate (not the oscillator's)
    const rs = window.__routes(); const idx = rs.findIndex(x => x.s === 'lfo2'); if (idx >= 0) try { window.__tiSetBypass(idx, 1); } catch(e){}
    const mods = window.__tiOscRoutes('a');
    const dispA = document.querySelector('#osc-a-device .osc-display'), dispB = document.querySelector('#osc-b-device .osc-display');
    const open = (d) => { const q = d.getBoundingClientRect(); d.dispatchEvent(new MouseEvent('contextmenu', { bubbles:true, cancelable:true, clientX:q.left + 20, clientY:q.top + 10, view:window }));
      const m = document.querySelector('.samp-menu.open'); return m ? [...m.children].map(c => c.textContent.trim()).filter(Boolean) : []; };
    const rowsA = open(dispA); const cw = [...document.querySelector('.samp-menu.open').children].find(c => /^Copy with modulators/.test(c.textContent.trim())); if (cw) cw.click();
    const clip = window.__oscClip ? { from: window.__oscClip.from, n: window.__oscClip.mods ? window.__oscClip.mods.length : -1 } : null;
    const rowsB = open(dispB); window.__natives.length = 0;
    const pr = [...document.querySelector('.samp-menu.open').children].find(c => /^Paste oscillator A/.test(c.textContent.trim())); if (pr) pr.click();
    const call = window.__natives.filter(n => n.fn === 'copyOscParams').map(n => n.args.join('>'));
    return new Promise(res => setTimeout(() => res({ mods: mods.map(m => m.sfx + ':' + Object.keys(m.S)[0] + Object.values(m.S)[0] + '@' + m.depth + (m.byp ? '·byp' : '')).sort(), rowsA: rowsA.filter(x => /oscillator|modulators/.test(x)), clip, rowsB: rowsB.filter(x => /^Paste oscillator/.test(x)), call,
      onB: window.__routes().filter(x => x.d === 10 || x.d === 11).map(x => x.s + '→' + x.d + '@' + x.v + (x.b ? '·byp' : '')).sort(), onA: window.__routes().filter(x => x.d === 3 || x.d === 4).length, rate: window.__routes().filter(x => x.d === 673).length }), 60)); });
  chk(r.mods && r.mods.join() === 'FOLD_AMT:lfo2@0.5·byp,WARP_AMOUNT:env1@1' && r.rowsA.join('|') === 'Copy oscillator|Copy with modulators' && r.clip && r.clip.from === 'a' && r.clip.n === 2,
      '14 osc A\'s menu: Copy oscillator · Copy with modulators — the copy carries Warp A + Fold A (bypass kept), not the LFO-rate route', JSON.stringify({ mods:r.mods, rowsA:r.rowsA, clip:r.clip }));
  chk(r.rowsB.join() === 'Paste oscillator A' && r.call.join() === 'a>b' && r.onB.join() === 'env1→10@1,lfo2→11@0.5·byp' && r.onA === 2 && r.rate === 1,
      '14 osc B\'s menu: Paste oscillator A → copyOscParams(a,b) and the two routes land on Warp B + Fold B with their depths; A keeps its own', JSON.stringify({ rowsB:r.rowsB, call:r.call, onB:r.onB, onA:r.onA, rate:r.rate }));

  // ── 8 · NO REGRESSION ───────────────────────────────────────────────────────────────────
  s = await pg.evaluate(() => { window.__mClose(); const svg = document.querySelector('#syn-panel .device.envs svg'); if (! svg) return { act:false, rows:['no env svg'] }; return window.__mOpen(svg); });
  chk(s.act && s.rows.some(x => x === 'Loop') && ! s.rows.some(x => x.startsWith('Modulate')), '8  the envelope graph is not a destination: its own ENVELOPE menu still opens', JSON.stringify(s.rows.slice(0, 4)));
  const curveRows = await pg.evaluate((sel) => { window.__mClose(); window.__clearRoutes(); window.__tiAddRoute(1, 0, 3); const st = window.__mOpen(document.querySelector(sel)); window.__mClose(); return st.rows.filter(x => /· +Curve$/.test(x)); }, WARP);
  chk(curveRows.length === 0, '8  the fb554 "· Curve" rows are gone (the ⤢ on the route row is the curve now)', JSON.stringify(curveRows));
  // fb462's floor law: zero NEW errors. The one error this stub provokes on the UNCHANGED page is
  // the sampler page's formatOutput() (hero output readout, index.html ~14107) reading a value the
  // stub never gives it — present on HEAD before fb563, verified by booting HEAD's page with this
  // exact stub. It is named here so it cannot hide anything else.
  const stacks = await pg.evaluate(() => window.__errStacks || []);
  const preExisting = (i) => /formatOutput/.test(stacks[i] || '');
  const fresh = errs.filter((e, i) => ! preExisting(i));
  chk(fresh.length === 0, '8  zero new page errors', (errs.length - fresh.length) + ' pre-existing (formatOutput) · ' + fresh.length + ' new' + (fresh.length ? '\n         ' + JSON.stringify(fresh.slice(0, 3)) + '\n         ' + JSON.stringify(stacks.slice(0, 3)) : ''));

  await b.close();
  console.log('\n  ' + pass + ' pass · ' + fail + ' fail' + (MUT ? '   (mutation ' + MUT + ' — a red bar here is the point)' : '') + '\n');
  if (MUT) { if (fail === 0) { console.log('  ✗ MUTATION ' + MUT + ' DID NOT RED ANY BAR'); process.exit(1); } process.exit(0); }
  process.exit(fail ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
