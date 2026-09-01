// all_menus — "ALL" AT THE TOP OF EVERY FOLDER MENU, AND THE COUNT TRAP CLOSED FOR GOOD.
//
// Max (fb524): "There needs to also be like an ALL section for our menus. Filters, wavetables,
// all that shit. It's just all wavetables, INCLUDING THE USER ONES. Every single folder, every
// single subfolder, at the top is the top of the folder name ALL."
//
// The feature is one line of UI. The RISK is not: index.html's generic binder normalised a choice
// write by `el.options.length` — the DOM row count — so an "All" built by DUPLICATING <option>s
// would have doubled that count and silently written the WRONG PARAMETER VALUE on every selection
// in that menu. That exact shape (fb373 · fb413 · the wavetable menu's 16172) has bitten this
// project three times. So this suite gates BOTH: that All is there and correct, and that the trap
// it could have walked into is now unreachable rather than merely unvisited.
//
//   NODE_PATH=<scratchpad>/node_modules node Tests/all_menus.js [page.html]
//
// 🚨 THE VIEWPORT IS THE SHIPPED ONE (fb454's law): 820 × 656.
//
// THE BARS
//   1  ALL EXISTS, FIRST — every folder browser in the plugin renders "All" as the TOP row of its
//      category column, with the total count beside it. Not a sample of them: the full set the
//      sweep found (warp · main filter · FX filter · wavetable · sample · noise), plus a SOURCE
//      gate that no openTwoPaneBrowser call site opts out.
//   2  ALL IS THE UNION — All's rows are exactly the concatenation of every other folder's rows,
//      in folder order, no duplicates, nothing missing.
//   3  SAME PICK, SAME VALUE (the fb373 gate) — for EVERY item in EVERY menu, picking it out of
//      All must produce the IDENTICAL backend traffic (parameter writes + native calls, values
//      included) as picking it out of its own folder. A menu that merely LOOKS right proves
//      nothing; only the readback does.
//   4  THE USER'S TABLES ARE IN IT — the import registry's folders and loose files appear in All,
//      and when that registry GROWS at runtime the new entries join All while every factory entry
//      still writes the value it wrote before. A user import may never renumber the factory.
//   5  THE COUNT TRAP IS CLOSED — with 30 extra duplicate <option> rows shoved into a live bound
//      <select> at runtime, the wavetable write must still land on idx/(29). The old code divided
//      by options.length and would land on idx/(59). Also: __synChoiceCount prefers the PARAMETER's
//      cardinality over the DOM's and shouts when they disagree (it already catches the shipped
//      6-option <select> bound to the 7-choice SYN_OSC_A_ENGINE).
//   6  NO REGRESSION — zero NEW page errors against the pre-change page (fb462's floor law), and
//      search still lists each hit ONCE (All mirrors the folders; searching both would double it).
//
// PROOF THE BARS CAN FAIL (fb421 — a gate that has never failed has never been tested):
//   ALLM_MUTATE=1  deletes the All injection from the house browser        → bars 1/2/3/4 fail.
//   ALLM_MUTATE=2  breaks the INDEX MAPPING: All keeps every name but each row picks its
//                  NEIGHBOUR's action. The menu still looks perfect.       → bar 3 fails.
//   ALLM_MUTATE=3  reverts the binder to `el.options.length`               → bar 5 fails.
//   ALLM_MUTATE=4  lets search walk the All category too                   → bar 6 fails.
const puppeteer = require('puppeteer-core');
const fs   = require('fs');
const path = require('path');
const ROOT = path.join(__dirname, '..');
const SRC  = process.argv[2] || process.env.ALLM_PAGE || path.join(ROOT, 'Source/ui/public/index.html');
const MUT  = +(process.env.ALLM_MUTATE || 0);
const VW = 820, VH = 656, DSF = 2;
const OUT = process.env.ALLM_TMP || require('os').tmpdir();

let pass = 0, fail = 0;
const chk = (ok, label, detail) => { if (ok) { pass++; console.log('  ok    ' + label + (detail ? '   ' + detail : '')); }
  else { fail++; console.log('  FAIL  ' + label + (detail ? '   ' + detail : '')); } };

// ── the page, mutated at SOURCE (a mutation the runtime could undo is no proof) ─────────────
const ANCHOR_INJECT =
`      if (cfg.all !== false) {
        var __c0 = cfg.cats || [];
        cfg.cats = window.__catsWithAll (__c0);
        if (cfg.cats.length > __c0.length && typeof cfg.openCat === 'number' && cfg.openCat >= 0) cfg.openCat += 1;
      }`;
const ANCHOR_UNION =
`      cats.forEach (function (c) { if (c && ! c.isAll) (c.items || []).forEach (function (it) { all.push (it); }); });`;
const ANCHOR_COUNT =
`              const numChoices = __synChoiceCount (paramId, el);`;
const ANCHOR_SEARCH =
`          if (cat && cat.isAll) return;   // All MIRRORS the folders below it; searching both would list every hit twice`;

let PAGE = SRC;
if (MUT) {
  let h = fs.readFileSync(SRC, 'utf8');
  const swap = (a, b, tag) => { if (h.indexOf(a) < 0) throw new Error('MUT ' + MUT + ': anchor not found — ' + tag); h = h.replace(a, b); };
  if (MUT === 1) swap(ANCHOR_INJECT, '      /* All injection deleted by ALLM_MUTATE=1 */', 'inject');
  if (MUT === 2) swap(ANCHOR_UNION,
    `      cats.forEach (function (c) { if (c && ! c.isAll) (c.items || []).forEach (function (it) {
        all.push ({ name: it.name, sel: it.sel, pick: (all.length ? all[all.length - 1].pick : it.pick) }); }); });`, 'union');
  if (MUT === 3) swap(ANCHOR_COUNT, '              const numChoices = el.options.length;', 'count');
  if (MUT === 4) swap(ANCHOR_SEARCH, '          /* isAll skip removed by ALLM_MUTATE=4 */', 'search');
  PAGE = path.join(OUT, 'all_menus_mut' + MUT + '.html');
  fs.writeFileSync(PAGE, h);
}

// ── THE STUB — real stepped choice params, and a backend that RECORDS every write and every
//    native call. A pick is only "the same pick" if the traffic it produced is byte-identical,
//    and half these menus (sample / noise / wavetable imports) talk to natives, not params. ──
// The warp lane's cardinality is read from the C++, never retyped — a stub kinder (or crueller)
// than the backend is worse than no stub (fb393), and a warp desync makes the picker REFUSE to
// open, which would look like "All is missing" rather than "the harness lied".
const WARP_N = (() => {
  const s = fs.readFileSync(path.join(ROOT, 'Source/PluginProcessor.cpp'), 'utf8');
  const m = /for \(int i = w\.size\(\); i < (\d+); \+\+i\) w\.add \("Reserved "/.exec(s);
  if (!m) throw new Error('warp cardinality not found in PluginProcessor.cpp');
  return +m[1];
})();
// 🚨 fb530 — THE WAVETABLE COUNT IS READ FROM THE SOURCE, NEVER TYPED HERE.
// This stub used to hardcode 30, and when the bank grew to 46 the harness reported a desync that
// did not exist — a stub kinder (or here, staler) than the backend is worse than no stub at all.
// A hardcoded number here is an ELEVENTH site in the ten-site list, so it is derived like WARP_N.
const WT_N = (() => {
  const s = fs.readFileSync(path.join(ROOT, 'Source/PluginProcessor.cpp'), 'utf8');
  const a = s.indexOf('ParameterIDs::SYN_OSC_A_WT_PRESET, 1 }');
  if (a < 0) throw new Error('SYN_OSC_A_WT_PRESET parameter not found in PluginProcessor.cpp');
  const b = s.indexOf('juce::StringArray {', a);
  const e = s.indexOf('},', b);
  if (b < 0 || e < 0) throw new Error('SYN_OSC_A_WT_PRESET StringArray not found');
  const body = s.slice(b, e).replace(/\/\/[^\n]*/g, '');          // drop // comments, keep the names
  const names = body.match(/"(?:[^"\\]|\\.)*"/g) || [];
  if (names.length < 2) throw new Error('SYN_OSC_A_WT_PRESET StringArray looks empty');
  return names.length;
})();
const CHOICE = {};
['A','B','C','D'].forEach((o) => { CHOICE['SYN_OSC_' + o + '_WARP_MODE'] = WARP_N; CHOICE['SYN_OSC_' + o + '_WARP2_MODE'] = WARP_N;
                                   CHOICE['SYN_OSC_' + o + '_WT_PRESET'] = WT_N; CHOICE['SYN_OSC_' + o + '_ENGINE'] = 7; });
CHOICE['SYN_FILTER_TYPE'] = 94; CHOICE['SYN_FILTER2_TYPE'] = 94; CHOICE['SYN_NOISE_TYPE'] = 13;

// The import registries the natives hand back. USER content: one referenced FOLDER and two loose
// files per kind — exactly the shape the real backend returns.
const REG = {
  wt:    { folders: [{ name:'My Tables', path:'/u/My Tables', items:[{name:'Zeta Sweep',path:'/u/My Tables/Zeta Sweep.wav'},
                                                                    {name:'Ur Growl',  path:'/u/My Tables/Ur Growl.wav'}] }],
           files:   [{ name:'Hand Drawn', path:'/u/Hand Drawn.wav' }] },
  samp:  { folders: [{ name:'My Samples', path:'/u/My Samples', items:[{name:'Kick 909', path:'/u/My Samples/Kick 909.wav'}] }],
           files:   [{ name:'Vox Take 3', path:'/u/Vox Take 3.wav' }] },
  noise: { folders: [{ name:'My Noise', path:'/u/My Noise', items:[{name:'Room Tone', path:'/u/My Noise/Room Tone.wav'}] }],
           files:   [{ name:'Fridge Hum', path:'/u/Fridge Hum.wav' }] }
};

const STUB = (cfg) => {
  const CH = cfg.choiceParams;
  window.__emits = [];      // every parameter write that reached "the backend"
  window.__natives = [];    // every native call, with its arguments
  window.__reg = cfg.reg;   // mutable at runtime — bar 4 grows it and reopens
  const states = new Map();
  const mkChoice = (name, n) => {
    const props = { start:0, end:n-1, skew:1, name:name, label:'', numSteps:n, interval:1, parameterIndex:states.size };
    const st = { name:name, scaledValue:0, properties:props,
      getScaledValue(){ return st.scaledValue; }, setScaledValue(v){ st.scaledValue = v; },
      getNormalisedValue(){ return (st.scaledValue - props.start) / (props.end - props.start); },
      setNormalisedValue(v){
        const raw = v * (props.end - props.start) + props.start;
        const snap = Math.max(props.start, Math.min(props.end,
                       props.start + props.interval * Math.floor((raw - props.start) / props.interval + 0.5)));
        st.scaledValue = snap;
        window.__emits.push({ name:name, norm:+v.toFixed(9), value:snap });
        (st.__ls || []).forEach((f) => { try { f(); } catch(e){} });
      },
      valueChangedEvent:{ addListener(f){ (st.__ls = st.__ls || []).push(f); return {remove(){}}; }, removeListener(){} },
      propertiesChangedEvent:{ addListener(){ return {remove(){}}; }, removeListener(){} },
      getChoiceIndex:()=>st.scaledValue, setChoiceIndex(i){ st.scaledValue = i; },
      getValue:()=>false, setValue(){}, sliderDragStarted(){}, sliderDragEnded(){} };
    return st;
  };
  const mkPlain = (name) => ({ getScaledValue:()=>0.5, setScaledValue(){},
    getNormalisedValue:()=>0.5, setNormalisedValue(v){ window.__emits.push({name:name, norm:+v.toFixed(9), value:null}); },
    getChoiceIndex:()=>0, setChoiceIndex(){}, getValue:()=>false, setValue(){},
    sliderDragStarted(){}, sliderDragEnded(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,skew:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:-1} });
  const get = (name) => { if (!states.has(name)) states.set(name, CH[name] ? mkChoice(name, CH[name]) : mkPlain(name)); return states.get(name); };
  window.__stubState = get;

  // A factory sample library with two categories, so the sample browser has real folders to unite.
  const FACTJSON = JSON.stringify({ path:'/factory', cats:{ 'Drums':['Kick.wav','Snare.wav'], 'Textures':['Air.wav','Grit.wav','Hum.wav'] } });
  const nativeFn = (n) => (...a) => new Promise((r) => {
    window.__natives.push({ fn:n, args:a.map((x) => (typeof x === 'object' ? JSON.stringify(x) : String(x))) });
    if (/^listWtImports$/.test(n))      return r(JSON.stringify(window.__reg.wt));
    if (/^listSampleImports$/.test(n))  return r(JSON.stringify(window.__reg.samp));
    if (/^listNoiseImports$/.test(n))   return r(JSON.stringify(window.__reg.noise));
    if (/^scanNoiseFactory$/.test(n))   return r(FACTJSON);
    if (/^listImports$/.test(n))        return r('[]');          // the legacy Wavetables-folder drop list
    if (/getPresets/i.test(n))          return r('[]');
    if (/Json|JSON/.test(n))            return r('{}');
    r(0);
  });
  window.Juce = { getSliderState:get, getToggleState:get, getComboBoxState:get, getNativeFunction:nativeFn,
                  backend:{addEventListener(){},removeEventListener(){},emitEvent(){}} };
  (function(){ const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable:true,
    get(){ return held; }, set(v){ held = Object.assign({}, v||{}, { getNativeFunction:mine.getNativeFunction, getSliderState:mine.getSliderState }); } }); })();
  window.__JUCE__ = { backend:window.Juce.backend, initialisationData:{ vendor:'', pluginName:'', pluginVersion:'',
    __juce__sliders:[], __juce__toggles:[], __juce__comboBoxes:[], __juce__functions:[] } };
  Element.prototype.setPointerCapture = function(){};
  Element.prototype.releasePointerCapture = function(){};
};

// ── in-page helpers ─────────────────────────────────────────────────────────────────────────
const HELPERS = () => {
  window.__amPanes = () => [...document.querySelectorAll('.tpb-pane')];
  // the house row keeps its LABEL in a flex:1 span (the • dot and the count are their own spans),
  // so read THAT — a name ending in a digit ("Diode 1") is not a count.
  window.__amRows = (pane) => [...pane.children].map((d) => ({ el:d,
    name: (d.querySelector('span[style*="flex:1"]') || d).textContent.trim(),
    count: (() => { const s = [...d.querySelectorAll('span')].pop();
                    return (s && s !== d.querySelector('span[style*="flex:1"]') && /^\d+$/.test(s.textContent.trim())) ? +s.textContent.trim() : null; })() }));
  window.__amCats  = () => { const p = window.__amPanes(); return p.length < 2 ? null : window.__amRows(p[0]); };
  window.__amItems = () => { const p = window.__amPanes(); return p.length < 2 ? null : window.__amRows(p[1]); };
  window.__amClose = () => { try { if (window.__tpbClose) window.__tpbClose(); } catch(e){}
                             try { if (window.__synHideMenu) window.__synHideMenu(); } catch(e){} };
  window.__amEv = (x, y) => ({ clientX:x||300, clientY:y||200, preventDefault(){}, stopPropagation(){} });
  // WARP 2's pill is engine-conditional; park every osc on the WAVETABLE engine so the menus under
  // test are the ones under test.
  window.__amWavetable = () => [...document.querySelectorAll('#syn-panel .device.osc')].forEach((d) =>
    ['engine-sample','engine-granular','engine-geode','engine-harm','engine-modal','engine-fm'].forEach((c) => d.classList.remove(c)));

  // OPEN one named browser. Returns the category names, or an error string.
  window.__amOpen = (which) => {
    window.__amClose(); window.__amWavetable();
    try {
      if (which === 'warp') {
        const el = document.querySelector('#syn-panel [data-syn="SYN_OSC_A_WARP_AMOUNT"]'); if (!el) return 'no warp knob';
        el.dispatchEvent(new MouseEvent('contextmenu', { bubbles:true, cancelable:true, clientX:300, clientY:200 }));
        // fb563 — the right-click opens the CONTROL MENU; the warp browser is its "Warp mode ›" row
        const cm = document.getElementById('syn-ctx-menu');
        const row = cm ? [...cm.querySelectorAll('.syn-ctx-item')].find((d) => /^Warp mode/.test(d.textContent.trim())) : null;
        if (! row) return 'no Warp mode row in the control menu';
        row.click();
      } else if (which === 'filter')   { if (!window.__fltTypeMenuOpen) return 'no __fltTypeMenuOpen'; window.__fltTypeMenuOpen(window.__amEv()); }
      else if (which === 'fxfilter')   { if (!window.__fltOpenBrowser) return 'no __fltOpenBrowser';
                                         window.__fltOpenBrowser(window.__amEv(), window.__amFxDev()); }
      else if (which === 'wavetable')  { if (!window.openWtSelectMenu) return 'no openWtSelectMenu'; window.openWtSelectMenu('a', window.__amEv()); }
      else if (which === 'sample')     { if (!window.openSampleBrowser) return 'no openSampleBrowser'; window.openSampleBrowser('a', window.__amEv()); }
      else if (which === 'noise')      { const nm = document.getElementById('noise-name'); if (!nm) return 'no #noise-name';
                                         nm.dispatchEvent(new MouseEvent('mousedown', { bubbles:true, cancelable:true, button:0, clientX:300, clientY:200 })); }
      else return 'unknown browser ' + which;
    } catch (e) { return 'threw: ' + String(e).slice(0, 120); }
    return 'opened';
  };
  // The FX filter card's browser wants a device record. Use the rack's own if one exists; otherwise
  // a minimal stand-in with the same shape, so the browser under test is the real one either way.
  window.__amFxDev = () => ({ type:(window.FLT_ENGINES && window.FLT_ENGINES[0]) || 'Ladder LP 24',
                              pfx:'SYN_FXFLT_1_', back:{ d1:{ v:'' } } });

  // CLICK a category by name, return its item rows' names.
  window.__amPickCat = (name) => { const cats = window.__amCats(); if (!cats) return null;
    const c = cats.find((r) => r.name === name); if (!c) return null; c.el.click();
    return (window.__amItems() || []).map((r) => r.name); };
  // CLICK item #i of category `cat` and return the backend traffic it caused.
  window.__amPickItem = (cat, i) => {
    const cats = window.__amCats(); if (!cats) return { err:'no browser' };
    const c = cats.find((r) => r.name === cat); if (!c) return { err:'no category ' + cat }; c.el.click();
    const items = window.__amItems() || []; if (!items[i]) return { err:'no item ' + i + ' in ' + cat };
    const name = items[i].name;
    window.__emits.length = 0; window.__natives.length = 0;
    items[i].el.click();
    return { name:name, emits:window.__emits.slice(), natives:window.__natives.slice() };
  };
  window.__amSearch = (q) => { const s = document.querySelector('.tpb-srch'); if (!s) return null;
    s.value = q; s.oninput(); return (window.__amItems() || []).map((r) => r.name); };
};

const BROWSERS = ['warp', 'filter', 'fxfilter', 'wavetable', 'sample', 'noise'];
const settle = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  const b = await puppeteer.launch({ executablePath:(process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless:'new', args:['--no-sandbox','--allow-file-access-from-files'] });

  // ── THE FLOOR (fb462) — a page error only counts if the PRE-CHANGE page did not already throw
  //    it. Without a floor there is no floor, and the bar says so instead of passing vacuously.
  const PRE = process.env.ALLM_PREPAGE || '';
  let baseErrs = null;
  if (PRE && fs.existsSync(PRE)) {
    const p0 = await b.newPage(); await p0.setViewport({ width:VW, height:VH, deviceScaleFactor:DSF });
    const e0 = []; p0.on('pageerror', (e) => e0.push(String(e).slice(0, 200)));
    await p0.evaluateOnNewDocument(STUB, { choiceParams:CHOICE, reg:REG });
    await p0.goto('file://' + PRE, { waitUntil:'load', timeout:60000 });
    await settle(1400);
    await p0.evaluate(() => { const sp = document.getElementById('syn-panel'); if (sp) sp.style.display = 'block'; window.dispatchEvent(new Event('resize')); });
    await settle(700);
    baseErrs = [...new Set(e0)]; await p0.close();
  }

  const pg = await b.newPage(); await pg.setViewport({ width:VW, height:VH, deviceScaleFactor:DSF });
  const errs = []; pg.on('pageerror', (e) => errs.push(String(e).slice(0, 200)));
  await pg.evaluateOnNewDocument(STUB, { choiceParams:CHOICE, reg:REG });
  await pg.evaluateOnNewDocument(HELPERS);
  await pg.goto('file://' + PAGE, { waitUntil:'load', timeout:60000 });
  await settle(1400);
  await pg.evaluate(() => { const sp = document.getElementById('syn-panel'); if (sp) sp.style.display = 'block'; window.dispatchEvent(new Event('resize')); });
  await settle(800);

  console.log('\n══ ALL — the union folder, at the top of every menu — ' + (MUT ? ('MUTATION ' + MUT) : 'the shipped page') + ' ══');
  console.log('   page ' + PAGE);
  console.log('   viewport ' + VW + '×' + VH + '\n');

  // ── SOURCE GATE — the full set, not a sample (fb425). Every openTwoPaneBrowser call site must
  //    route through the ONE injection point, and none may opt out with `all:false`. ──
  {
    const h = fs.readFileSync(PAGE, 'utf8');
    const sites = (h.match(/openTwoPaneBrowser\s*\(/g) || []).length;   // the definition is `= function (`, so it does not match
    const optouts = (h.match(/all\s*:\s*false/g) || []).length;
    const helpers = (h.match(/window\.__catsWithAll\s*=/g) || []).length;
    chk(sites >= 6, 'SOURCE — every folder browser in the plugin goes through the one house component', sites + ' openTwoPaneBrowser call sites');
    chk(optouts === 0, 'SOURCE — no call site opts out of All (`all:false`)', optouts + ' opt-outs');
    chk(helpers === 1, 'SOURCE — All is built in exactly ONE place (no second copy to drift)', helpers + ' definitions of __catsWithAll');
  }

  const seen = {};
  for (const which of BROWSERS) {
    const opened = await pg.evaluate((w) => window.__amOpen(w), which);
    await settle(320);                                          // the import/factory natives are async
    const cats = await pg.evaluate(() => { const c = window.__amCats(); return c ? c.map((r) => ({ name:r.name, count:r.count })) : null; });
    if (opened !== 'opened' || !cats) { chk(false, 'ALL EXISTS — ' + which, 'browser did not open (' + opened + ')'); await pg.evaluate(() => window.__amClose()); continue; }
    seen[which] = cats;

    // ── 1  ALL EXISTS, FIRST ────────────────────────────────────────────────────────────────
    chk(cats[0] && cats[0].name === 'All', 'ALL EXISTS — ' + which + ': the TOP row of the folder column is "All"',
        'cats [' + cats.map((c) => c.name).join(' · ') + ']');

    // ── 2  ALL IS THE UNION ─────────────────────────────────────────────────────────────────
    const union = await pg.evaluate(() => {
      const cats = window.__amCats(); if (!cats) return null;
      const all = window.__amPickCat('All') || [];
      const rest = []; cats.slice(1).forEach((c) => { (window.__amPickCat(c.name) || []).forEach((n) => rest.push(n)); });
      return { all:all, rest:rest };
    });
    chk(union && JSON.stringify(union.all) === JSON.stringify(union.rest),
        'UNION — ' + which + ': All is exactly every folder\'s rows, in folder order',
        union ? ('All ' + union.all.length + ' rows vs folders ' + union.rest.length + ' rows'
                 + (JSON.stringify(union.all) === JSON.stringify(union.rest) ? ''
                    : '  first divergence @ ' + union.all.findIndex((n, i) => n !== union.rest[i]))) : 'no browser');
    chk(cats[0] && union && cats[0].count === union.rest.length,
        'UNION — ' + which + ': the All row carries the total count', (cats[0] ? cats[0].count : '—') + ' vs ' + (union ? union.rest.length : '—'));

    // ── 3  SAME PICK, SAME VALUE — every item, both routes (fb373 / fb425) ──────────────────
    {
      const folderOf = [];   // per union index: [category name, index within that category]
      let k = 0;
      for (let ci = 1; ci < cats.length; ci++) {
        const n = await pg.evaluate((nm) => (window.__amPickCat(nm) || []).length, cats[ci].name);
        for (let j = 0; j < n; j++) folderOf[k++] = [cats[ci].name, j];
      }
      const misses = [];
      for (let i = 0; i < folderOf.length; i++) {
        const viaAll = await pg.evaluate((i) => window.__amPickItem('All', i), i);
        const viaFld = await pg.evaluate((c, j) => window.__amPickItem(c, j), folderOf[i][0], folderOf[i][1]);
        const key = (r) => JSON.stringify({ e:r.emits, n:r.natives });
        if (viaAll.err || viaFld.err) { misses.push(i + ': ' + (viaAll.err || viaFld.err)); continue; }
        if (viaAll.name !== viaFld.name) { misses.push(i + ': All shows "' + viaAll.name + '", the folder shows "' + viaFld.name + '"'); continue; }
        if (key(viaAll) !== key(viaFld))
          misses.push(viaAll.name + ': All wrote ' + key(viaAll).slice(0, 90) + ' · the folder wrote ' + key(viaFld).slice(0, 90));
      }
      chk(misses.length === 0 && folderOf.length > 0,
          'SAME PICK — ' + which + ': every item writes the SAME parameter value from All as from its folder',
          misses.length ? ((folderOf.length - misses.length) + '/' + folderOf.length + ' ok · ' + misses.slice(0, 2).join(' · ')
                           + (misses.length > 2 ? ' …+' + (misses.length - 2) : ''))
                        : folderOf.length + '/' + folderOf.length + ' items round-tripped');
    }

    // ── 6b  SEARCH IS NOT DOUBLED BY ALL ────────────────────────────────────────────────────
    if (union && union.rest.length) {
      const probe = union.rest[0];
      const hits = await pg.evaluate((q) => window.__amSearch(q), probe.toLowerCase());
      const want = union.rest.filter((n) => n.toLowerCase().indexOf(probe.toLowerCase()) >= 0).length;
      chk(hits && hits.length === want, 'NO REGRESSION — ' + which + ': search lists each hit once (All does not double it)',
          'query "' + probe + '" → ' + (hits ? hits.length : 'null') + ' rows, expected ' + want);
    }
    await pg.evaluate(() => window.__amClose());
  }

  // ── 4  THE USER'S TABLES ARE IN IT, AND GROWING THEM RENUMBERS NOTHING ─────────────────────
  {
    const USER = { wavetable:['Zeta Sweep','Ur Growl','Hand Drawn'], sample:['Kick 909','Vox Take 3'], noise:['Room Tone','Fridge Hum'] };
    for (const which of Object.keys(USER)) {
      const o = await pg.evaluate((w) => window.__amOpen(w), which); await settle(320);
      const all = await pg.evaluate(() => window.__amPickCat('All'));
      const missing = USER[which].filter((n) => !all || all.indexOf(n) < 0);
      chk(o === 'opened' && missing.length === 0, 'USER — ' + which + ': the imported folder AND the loose files are in All',
          missing.length ? 'missing [' + missing.join(', ') + ']' : USER[which].join(' · ') + ' all present');
      await pg.evaluate(() => window.__amClose());
    }
    // GROW the registry the way a real import does, reopen, and prove (a) the newcomer joined All
    // and (b) the FACTORY entry at index 0 still writes exactly what it wrote before it grew.
    // Probe a MID-LIST factory row (index 7, "OB-X Saw" → value 7). Row 0 would survive a
    // renumber by accident; row 7 cannot.
    const before = await pg.evaluate(async () => { window.__amOpen('wavetable'); await new Promise(r=>setTimeout(r,320));
      const r = window.__amPickItem('All', 7); window.__amClose(); return r; });
    await pg.evaluate(() => { window.__reg.wt.files.push({ name:'Fresh Import', path:'/u/Fresh Import.wav' }); });
    const after = await pg.evaluate(async () => { window.__amOpen('wavetable'); await new Promise(r=>setTimeout(r,320));
      const cats = window.__amCats(); const all = window.__amPickCat('All');
      const r = window.__amPickItem('All', 7); window.__amClose();
      return { all:all, first:r, cats:(cats||[]).map((c)=>c.name) }; });
    chk(after.all && after.all.indexOf('Fresh Import') >= 0, 'USER — All grows with the imports folder at runtime',
        after.all ? (after.all.length + ' rows, "Fresh Import" ' + (after.all.indexOf('Fresh Import') >= 0 ? 'present' : 'MISSING')) : 'no browser');
    chk(JSON.stringify(before.emits) === JSON.stringify(after.first.emits) && before.name === after.first.name,
        'USER — a new user table does NOT renumber the factory entries',
        'row 7 "' + before.name + '" wrote ' + JSON.stringify(before.emits) + ' before, ' + JSON.stringify(after.first.emits) + ' after');
  }

  // ── 5  THE COUNT TRAP IS CLOSED ───────────────────────────────────────────────────────────
  {
    // (a) the helper prefers the PARAMETER over the DOM, and says so out loud when they differ.
    const card = await pg.evaluate(() => {
      const sel = document.getElementById('osc-a-preset-select');
      const eng = document.querySelector('[data-syn="SYN_OSC_A_ENGINE"]');
      return { wt: window.__synChoiceCount ? window.__synChoiceCount('SYN_OSC_A_WT_PRESET', sel) : null,
               wtDom: sel ? sel.options.length : null,
               eng: (window.__synChoiceCount && eng) ? window.__synChoiceCount('SYN_OSC_A_ENGINE', eng) : null,
               engDom: eng ? eng.options.length : null,
               desync: Object.keys(window.__choiceDesync || {}) };
    });
    chk(card.wt === WT_N && card.wtDom === WT_N, 'COUNT TRAP — the wavetable list and its parameter agree (' + WT_N + ')',
        'param ' + card.wt + ' · dom ' + card.wtDom + '  [WT_N read from PluginProcessor.cpp]');
    chk(card.eng === 7 && card.engDom === 6, 'COUNT TRAP — a disagreeing list loses to the PARAMETER, and is reported',
        'SYN_OSC_A_ENGINE: param ' + card.eng + ' · dom ' + card.engDom + ' · flagged [' + card.desync.join(',') + ']');

    // (b) THE REAL SHAPE OF THE BUG, run live: shove a duplicate "All" optgroup into a BOUND
    //     <select> — 30 more rows, exactly what a careless All would have added — and drive the
    //     menu. The write must still be idx/(30−1). The old code gave idx/(60−1).
    await pg.evaluate((n) => { window.__WT_N = n; }, WT_N);
    const trap = await pg.evaluate(() => {
      const sel = document.getElementById('osc-a-preset-select'); if (!sel) return { err:'no select' };
      const g = document.createElement('optgroup'); g.label = 'All';
      for (let i = 0; i < window.__WT_N; i++) { const o = document.createElement('option'); o.value = String(i);
        o.textContent = 'dup ' + i; g.appendChild(o); }
      sel.insertBefore(g, sel.firstChild);
      const rows = sel.options.length;
      window.__emits.length = 0;
      sel.value = '7'; sel.dispatchEvent(new Event('change', { bubbles:true }));
      const e = window.__emits.filter((x) => x.name === 'SYN_OSC_A_WT_PRESET').pop();
      sel.removeChild(g);
      return { rows:rows, norm:e ? e.norm : null, value:e ? e.value : null };
    });
    const want = 7 / (WT_N - 1);
    chk(trap.value === 7 && Math.abs((trap.norm || 0) - want) < 1e-9,
        'COUNT TRAP — ' + WT_N + ' duplicate rows in a BOUND <select> cannot move the parameter',
        'options.length ' + trap.rows + ' → wrote norm ' + (trap.norm === null ? 'nothing' : trap.norm.toFixed(6))
        + ' (want ' + want.toFixed(6) + ' = 7/' + (WT_N - 1) + ', NOT ' + (7 / (2 * WT_N - 1)).toFixed(6)
        + ' = 7/' + (2 * WT_N - 1) + ') → index ' + trap.value);
  }

  // ── 6  NO REGRESSION — page errors against the floor ──────────────────────────────────────
  {
    const now = [...new Set(errs)];
    if (baseErrs === null) chk(false, 'NO REGRESSION — page errors measured against the PRE-CHANGE floor',
      'no ALLM_PREPAGE given — the floor is unknown, so this bar cannot pass (fb462)');
    else {
      const fresh = now.filter((e) => baseErrs.indexOf(e) < 0);
      chk(fresh.length === 0, 'NO REGRESSION — no page error this change did not inherit',
          'floor ' + baseErrs.length + ' · now ' + now.length + (fresh.length ? ' · NEW: ' + fresh.slice(0,2).join(' | ') : ' · new 0'));
    }
  }

  await b.close();
  console.log('\n  ' + pass + ' passed, ' + fail + ' failed'
    + (MUT ? '   (mutation ' + MUT + ' — failures here are the POINT)' : '') + '\n');
  process.exit(MUT ? (fail > 0 ? 0 : 1) : (fail > 0 ? 1 : 0));
})().catch((e) => { console.error(e); process.exit(2); });
