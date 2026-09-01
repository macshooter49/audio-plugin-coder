// warp_menu — THE WARP MENU: 48 VALUES, 35 LIVE MODES, ONE LIST, AND A GUARD THAT REDS.
//
// The warp mode list grew from 11 to 48 (11 live → 35 live + 13 reserved). The failure this
// suite exists to prevent is NOT "the menu looks wrong" — it is fb373 / the wavetable menu's
// 16172: a choice param written as idx/(count−1) against a JS list that still carries the OLD
// count. Index 29 of 48 written against a list of 11 is 2.9 → clamped → mode 47. It compiles,
// it renders, the label under the cursor is right, and the sound is somebody else's.
//
//   NODE_PATH=<scratchpad>/node_modules node Tests/warp_menu.js [page.html]
//
// 🚨 THE VIEWPORT IS THE SHIPPED ONE (fb454's law): 820 × 656.
//
// THE BARS
//   1  ONE LIST — window.WARP_MODES is 48 long and every name equals terrainWarpModeNames()
//      at the same index, parsed live out of PluginProcessor.cpp. No second warp array, no
//      `length === 11` literal, no inline fallback copy anywhere in the page source.
//   2  FAMILIES — every LIVE index (0..34) is filed in exactly one family, no reserved index
//      is listed, and the browser renders exactly those 35 rows with those names.
//   3  ROUND TRIP (the fb373 gate) — for EVERY live mode, on EVERY one of the 8 warp-mode
//      params, drive the real picker and read back what actually reached the parameter. The
//      scaled value must equal the C++ index. A green menu proves nothing; the readback does.
//   4  THE GUARD REDS — a deliberate desync (the parameter grows, the list does not) must
//      fail __warpGuard(), paint the banner, and make the picker REFUSE to open.
//   5  UNKNOWABLE ≠ WRONG — a param whose properties have not arrived (interval 0,
//      parameterIndex −1) is skipped by the guard, never guessed at. Boot must be green.
//   6  NO REGRESSION — zero page errors, and the syn ctx menu still serves the 6-entry SAMPLE
//      warp list flat (that list did not grow, so it must not have moved).
//
// PROOF THE BARS CAN FAIL (fb421 — a gate that has never failed has never been tested):
//   WARPM_MUTATE=1  truncates the registry back to the shipped 11 entries → bars 1/2/3/4 fail.
//   WARPM_MUTATE=2  unfiles a live mode (34 Exciter) from its family        → bars 2/4 fail.
//   WARPM_MUTATE=3  makes the knob writer normalise by a LITERAL 10         → bar 3 fails.
//   WARPM_MUTATE=4  grows the PARAMETER to 60 values behind the list's back → bar 4's live
//                   half fails on the shipped page (this is the real-world shape).
const puppeteer = require('puppeteer-core');
const fs   = require('fs');
const path = require('path');
const ROOT = path.join(__dirname, '..');
const SRC  = process.argv[2] || process.env.WARPM_PAGE || path.join(ROOT, 'Source/ui/public/index.html');
const CPP  = path.join(ROOT, 'Source/PluginProcessor.cpp');
const MUT  = +(process.env.WARPM_MUTATE || 0);
const VW = 820, VH = 656, DSF = 2;
const OUT = process.env.WARPM_TMP || require('os').tmpdir();

let pass = 0, fail = 0;
const chk = (ok, label, detail) => { if (ok) { pass++; console.log('  ok    ' + label + (detail ? '   ' + detail : '')); }
  else { fail++; console.log('  FAIL  ' + label + (detail ? '   ' + detail : '')); } };

// ── the C++ side, parsed (never retyped) ───────────────────────────────────────────────────
function cppWarpNames() {
  const s = fs.readFileSync(CPP, 'utf8');
  const i = s.indexOf('static juce::StringArray terrainWarpModeNames()');
  if (i < 0) throw new Error('terrainWarpModeNames() not found in ' + CPP);
  const body = s.slice(i, s.indexOf('\n}', i));
  const arr  = body.slice(body.indexOf('juce::StringArray w {'), body.indexOf('};'));
  const names = [];
  arr.replace(/^[^\n]*$/gm, (line) => { const c = line.indexOf('//'); const l = (c >= 0 ? line.slice(0, c) : line);
    l.replace(/"([^"]*)"/g, (_, n) => { names.push(n); return _; }); return line; });
  const cap = /for \(int i = w\.size\(\); i < (\d+); \+\+i\) w\.add \("Reserved " \+ juce::String \(i\)\)/.exec(body);
  const total = cap ? +cap[1] : names.length;
  for (let k = names.length; k < total; k++) names.push('Reserved ' + k);
  return names;
}
// The C++ display string for index 0 is the all-caps host name; the UI's type mandate spells it
// Title-case (only real acronyms take caps). The only sanctioned divergence, and it is by INDEX,
// so it can never move a selection.
const ALIAS = { 'NONE': 'None' };
const cppNames = cppWarpNames();
const cppUi = cppNames.map((n) => ALIAS[n] || n);

// ── the page, optionally mutated at SOURCE (a mutation the runtime could undo is no proof) ──
let PAGE = SRC;
if (MUT === 1 || MUT === 2 || MUT === 3) {
  let h = fs.readFileSync(SRC, 'utf8');
  if (MUT === 1) {
    const a = h.indexOf("      /* 11 */ 'Tube',");
    const b = h.indexOf("'Reserved 47'\n    ];");
    if (a < 0 || b < 0) throw new Error('MUT 1: registry anchors not found');
    h = h.slice(0, a) + h.slice(b + "'Reserved 47'\n".length);   // 11 entries, exactly what shipped
  }
  if (MUT === 2) h = h.replace("idx: [29, 30, 32, 33, 34] }", "idx: [29, 30, 32, 33] }");
  if (MUT === 3) h = h.replace("setChoiceValue(paramName, idx / Math.max(1, choiceCount(paramName, listLen) - 1));",
                               "setChoiceValue(paramName, idx / 10);");
  PAGE = path.join(OUT, 'warp_menu_mut' + MUT + '.html');
  fs.writeFileSync(PAGE, h);
}

// ── the stub: REAL choice params for the 8 warp mode lanes, and a backend that records ─────
const WARP_PARAMS = [];
['A','B','C','D'].forEach((o) => WARP_PARAMS.push('SYN_OSC_'+o+'_WARP_MODE', 'SYN_OSC_'+o+'_WARP2_MODE'));

const STUB = (cfg) => {
  const CH = cfg.choiceParams;               // name -> cardinality (a REAL stepped param)
  window.__emits = [];                        // every value that reached "the backend"
  const states = new Map();
  const mkChoice = (name, n) => {
    const props = { start: 0, end: n - 1, skew: 1, name: name, label: '', numSteps: n, interval: 1, parameterIndex: states.size };
    const st = {
      name: name, scaledValue: 0, properties: props,
      getScaledValue(){ return st.scaledValue; },
      setScaledValue(v){ st.scaledValue = v; },
      getNormalisedValue(){ return (st.scaledValue - props.start) / (props.end - props.start); },
      setNormalisedValue(v){
        const raw = Math.pow(v, 1 / props.skew) * (props.end - props.start) + props.start;
        const snap = Math.max(props.start, Math.min(props.end,
                       props.start + props.interval * Math.floor((raw - props.start) / props.interval + 0.5)));
        st.scaledValue = snap;
        window.__emits.push({ name: name, norm: v, value: snap });
        (st.__ls || []).forEach((f) => { try { f(); } catch(e){} });
      },
      valueChangedEvent:{ addListener(f){ (st.__ls = st.__ls || []).push(f); return {remove(){}}; }, removeListener(){} },
      propertiesChangedEvent:{ addListener(){ return {remove(){}}; }, removeListener(){} },
      getChoiceIndex:()=>st.scaledValue, setChoiceIndex(i){ st.scaledValue = i; },
      getValue:()=>false, setValue(){}, sliderDragStarted(){}, sliderDragEnded(){}
    };
    return st;
  };
  // an ordinary (unknowable) param: interval 0, parameterIndex −1 — the pre-answer state
  const mkPlain = (name) => ({ getScaledValue:()=>0.5, setScaledValue(){},
    getNormalisedValue:()=>0.5, setNormalisedValue(v){ window.__emits.push({name:name, norm:v, value:null}); },
    getChoiceIndex:()=>0, setChoiceIndex(){}, getValue:()=>false, setValue(){},
    sliderDragStarted(){}, sliderDragEnded(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,skew:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:-1} });
  const get = (name) => { if (!states.has(name)) states.set(name, CH[name] ? mkChoice(name, CH[name]) : mkPlain(name)); return states.get(name); };
  window.__stubState = get;
  window.Juce = { getSliderState:get, getToggleState:get, getComboBoxState:get,
    getNativeFunction:(n)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(n))return r('[]');
      if(/Json|JSON/.test(n))return r('{}'); r(0);}),
    backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
  (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,
    get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction,getSliderState:mine.getSliderState});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
    __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
  Element.prototype.setPointerCapture = function(){};
  Element.prototype.releasePointerCapture = function(){};
};

// ── in-page helpers ────────────────────────────────────────────────────────────────────────
const HELPERS = () => {
  window.__wmPanel = function(){ const p = document.querySelector('.tpb-pane'); return p ? p.parentNode.parentNode : null; };
  window.__wmPanes = function(){ return [...document.querySelectorAll('.tpb-pane')]; };
  // the house row puts its LABEL in a flex:1 span (the • dot and the count are their own spans),
  // so read THAT — a name ending in a digit ("Diode 1") is not a count.
  window.__wmRows  = function(pane){ return [...pane.children].map((d) => ({ el:d,
      name: (d.querySelector('span[style*="flex:1"]') || d).textContent.trim() })); };
  // the WARP 2 pill is engine-conditional: it serves the SAMPLE warp list while the osc carries an
  // engine-sample/granular/geode/harm class. Park every osc on the WAVETABLE engine to test WARP 2.
  window.__wmWavetable = function(){ [...document.querySelectorAll('#syn-panel .device.osc')].forEach((d) => {
      ['engine-sample','engine-granular','engine-geode','engine-harm'].forEach((c) => d.classList.remove(c)); }); };
  window.__wmOpen = function(sel, x, y){
    const el = document.querySelector(sel); if (!el) return 'no element ' + sel;
    el.dispatchEvent(new MouseEvent('contextmenu', {bubbles:true, cancelable:true, clientX:(x||300), clientY:(y||200)}));
    // fb563 — the right-click opens the CONTROL MENU first (Modulate · routes · Reset); the mode
    // list is its "Warp mode ›" / "Mode ›" row. Drive that row, exactly as a user would.
    const cm = document.getElementById('syn-ctx-menu');
    if (cm && cm.classList.contains('act')) {
      const row = [...cm.querySelectorAll('.syn-ctx-item')].find((d) => /^(Warp mode|Mode)/.test(d.textContent.trim()));
      if (row) row.click();
    }
    return window.__wmPanel() ? 'browser' : (document.getElementById('syn-ctx-menu')||{}).classList &&
           document.getElementById('syn-ctx-menu').classList.contains('act') ? 'ctxmenu' : 'none';
  };
  window.__wmPick = function(family, name){
    const panes = window.__wmPanes(); if (panes.length < 2) return 'no browser';
    const cats = window.__wmRows(panes[0]);
    const c = cats.find((r) => r.name === family); if (!c) return 'no family ' + family + ' in [' + cats.map(r=>r.name).join('|') + ']';
    c.el.click();
    const items = window.__wmRows(window.__wmPanes()[1]);
    const it = items.find((r) => r.name === name); if (!it) return 'no row ' + name;
    it.el.click(); return 'ok';
  };
  window.__wmClose = function(){ try { if (window.__tpbClose) window.__tpbClose(); } catch(e){}
    try { if (window.__synHideMenu) window.__synHideMenu(); } catch(e){} };
};

const KNOB = { 'SYN_OSC_A_WARP_MODE':'#syn-panel [data-syn="SYN_OSC_A_WARP_AMOUNT"]',
               'SYN_OSC_B_WARP_MODE':'#syn-panel [data-syn="SYN_OSC_B_WARP_AMOUNT"]',
               'SYN_OSC_C_WARP_MODE':'#syn-panel [data-syn="SYN_OSC_C_WARP_AMOUNT"]',
               'SYN_OSC_D_WARP_MODE':'#syn-panel [data-syn="SYN_OSC_D_WARP_AMOUNT"]' };
const PILL = { 'SYN_OSC_A_WARP2_MODE':'#osc-a-warp2-val', 'SYN_OSC_B_WARP2_MODE':'#osc-b-warp2-val',
               'SYN_OSC_C_WARP2_MODE':'#osc-c-warp2-val', 'SYN_OSC_D_WARP2_MODE':'#osc-d-warp2-val' };

(async () => {
  const cardinalities = {};
  WARP_PARAMS.forEach((p) => cardinalities[p] = cppNames.length);
  if (MUT === 4) cardinalities['SYN_OSC_A_WARP_MODE'] = 60;      // the parameter grew, the list did not
  // a couple of OTHER choice params so the generic helpers are exercised too
  cardinalities['SYN_OSC_A_SAMPLE_WARPMODE'] = 6;

  const b = await puppeteer.launch({ executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless:'new', args:['--no-sandbox','--allow-file-access-from-files'] });
  // ── THE FLOOR (fb462) — a page error only counts if the PRE-CHANGE page did not already throw
  //    it. WARPM_PREPAGE is the page as it stood before this wave; without one there is no floor
  //    and the bar says so instead of passing vacuously.
  const PRE = process.env.WARPM_PREPAGE || '';
  let baseErrs = null;
  if (PRE && fs.existsSync(PRE)) {
    const p0 = await b.newPage(); await p0.setViewport({width:VW, height:VH, deviceScaleFactor:DSF});
    const e0 = []; p0.on('pageerror', (e) => e0.push(String(e).slice(0,200)));
    await p0.evaluateOnNewDocument(STUB, { choiceParams: cardinalities });
    await p0.goto('file://' + PRE, { waitUntil:'load', timeout:60000 });
    await new Promise(r => setTimeout(r, 1400));
    await p0.evaluate(() => { const sp = document.getElementById('syn-panel'); if (sp) sp.style.display='block'; window.dispatchEvent(new Event('resize')); });
    await new Promise(r => setTimeout(r, 700));
    baseErrs = [...new Set(e0)]; await p0.close();
  }

  const pg = await b.newPage(); await pg.setViewport({width:VW, height:VH, deviceScaleFactor:DSF});
  const errs = []; pg.on('pageerror', (e) => errs.push(String(e).slice(0,200)));
  const cons = []; pg.on('console', (m) => { if (m.type() === 'error') cons.push(m.text().slice(0,200)); });
  await pg.evaluateOnNewDocument(STUB, { choiceParams: cardinalities });
  await pg.evaluateOnNewDocument(HELPERS);
  await pg.goto('file://' + PAGE, { waitUntil:'load', timeout:60000 });
  await new Promise(r => setTimeout(r, 1400));
  await pg.evaluate(() => { const sp = document.getElementById('syn-panel'); if (sp) sp.style.display='block'; window.dispatchEvent(new Event('resize')); });
  await new Promise(r => setTimeout(r, 700));

  console.log('\n══ THE WARP MENU — ' + (MUT ? ('MUTATION ' + MUT) : 'the shipped page') + ' ══');
  console.log('   page ' + PAGE);
  console.log('   viewport ' + VW + '×' + VH + '   C++ cardinality ' + cppNames.length + '\n');

  // ── 1  ONE LIST ────────────────────────────────────────────────────────────────────────
  const js = await pg.evaluate(() => ({ modes: window.WARP_MODES || null, count: window.WARP_MODE_COUNT || null,
                                        fams: (window.__warpFamilies || []).map((f) => ({ label:f.label, idx:f.idx.slice() })) }));
  chk(!!js.modes, 'ONE LIST — window.WARP_MODES exists', js.modes ? ('length ' + js.modes.length) : 'missing');
  chk(js.modes && js.modes.length === cppNames.length,
      'ONE LIST — the JS list is exactly the C++ cardinality',
      'js ' + (js.modes ? js.modes.length : 'n/a') + '   c++ ' + cppNames.length);
  {
    const bad = [];
    for (let i = 0; i < cppUi.length; i++) if (!js.modes || js.modes[i] !== cppUi[i]) bad.push(i + ': js "' + (js.modes ? js.modes[i] : '') + '" ≠ c++ "' + cppUi[i] + '"');
    chk(bad.length === 0, 'ONE LIST — every name matches terrainWarpModeNames() AT ITS INDEX',
        bad.length ? bad.slice(0,4).join(' · ') + (bad.length>4 ? ' …+'+(bad.length-4) : '') : cppUi.length + '/' + cppUi.length + ' identical (alias NONE→None)');
  }
  {
    const h = fs.readFileSync(PAGE, 'utf8');
    const dup = (h.match(/'Fractalize'/g) || []).length;
    chk(dup === 1, 'ONE LIST — no second warp array in the page source', "'Fractalize' occurrences: " + dup);
    // count LIVE code only — a `//` comment that quotes the old bug is documentation, not a copy.
    const code = h.split('\n').map((l) => { const c = l.indexOf('//'); return c >= 0 ? l.slice(0, c) : l; }).join('\n');
    const lit = (code.match(/WARP_MODES\.length\s*[=!]==?\s*\d+/g) || []);
    chk(lit.length === 0, 'ONE LIST — no hardcoded warp count literal survives in live code',
        lit.length ? lit.join(' · ') : 'zero `WARP_MODES.length === <n>` comparisons');
  }

  // ── 2  FAMILIES ────────────────────────────────────────────────────────────────────────
  const live = [];
  for (let i = 0; i < cppUi.length; i++) if (!/^Reserved /.test(cppUi[i])) live.push(i);
  {
    const filed = {}; let dupes = 0, offend = 0;
    js.fams.forEach((f) => f.idx.forEach((i) => { if (filed[i] != null) dupes++; else filed[i] = f.label;
                                                  if (/^Reserved /.test(cppUi[i] || 'Reserved x')) offend++; }));
    const missing = live.filter((i) => filed[i] == null);
    chk(missing.length === 0 && dupes === 0 && offend === 0,
        'FAMILIES — every live mode filed exactly once, no reserved slot listed',
        js.fams.length + ' families · filed ' + Object.keys(filed).length + '/' + live.length
        + ' · missing [' + missing.join(',') + '] · dupes ' + dupes + ' · reserved-listed ' + offend);
  }
  {
    const opened = await pg.evaluate((s) => { window.__wmWavetable(); return window.__wmOpen(s, 300, 220); }, KNOB['SYN_OSC_A_WARP_MODE']);
    const rows = await pg.evaluate(() => { const panes = window.__wmPanes(); if (panes.length < 2) return null;
      const cats = window.__wmRows(panes[0]); const all = [];
      // fb524 — the house browser now puts an "All" union folder at the top of EVERY category
      // column. It MIRRORS the families below it (same item objects, same pick), so totalling it
      // here would count every mode twice. Its own correctness is gated by Tests/all_menus.js;
      // this bar is about the FAMILIES, so it walks the families only.
      cats.filter((c) => c.name !== 'All')
          .forEach((c) => { c.el.click(); window.__wmRows(window.__wmPanes()[1]).forEach((r) => all.push(c.name + '/' + r.name)); });
      return { cats: cats.map((c)=>c.name), all: all }; });
    await pg.evaluate(() => window.__wmClose());
    chk(opened === 'browser', 'FAMILIES — right-click WARP opens the house two-pane browser (not a 770px flat list)', 'opened=' + opened);
    chk(rows && rows.all.length === live.length,
        'FAMILIES — the browser renders exactly the live modes',
        rows ? (rows.all.length + ' rows over ' + rows.cats.length + ' categories [' + rows.cats.join(' · ') + ']') : 'no browser');
    if (rows) {
      const names = rows.all.map((s) => s.split('/')[1]).sort();
      const want  = live.map((i) => cppUi[i]).sort();
      chk(JSON.stringify(names) === JSON.stringify(want), 'FAMILIES — every rendered row is a real C++ mode name',
          names.length + ' rendered');
    }
  }

  // ── 3  ROUND TRIP — the fb373 gate ─────────────────────────────────────────────────────
  const famOf = {}; js.fams.forEach((f) => f.idx.forEach((i) => famOf[i] = f.label));
  for (const param of WARP_PARAMS) {
    const sel = KNOB[param] || PILL[param];
    const isPill = !!PILL[param];
    const misses = [];
    for (const i of live) {
      const r = await pg.evaluate((s, fam, nm) => {
        window.__wmClose(); window.__wmWavetable();
        const o = window.__wmOpen(s, 300, 220);
        if (o !== 'browser') return { err: 'picker did not open (' + o + ')' };
        const p = window.__wmPick(fam, nm);
        return { pick: p };
      }, sel, famOf[i], cppUi[i]);
      if (r.err) { misses.push(cppUi[i] + ': ' + r.err); continue; }
      if (r.pick !== 'ok') { misses.push(cppUi[i] + ': ' + r.pick); continue; }
      const got = await pg.evaluate((p) => { const e = window.__emits.filter((x) => x.name === p);
        return { last: e.length ? e[e.length-1] : null, state: window.__stubState(p).getScaledValue() }; }, param);
      if (!got.last || got.last.value !== i || got.state !== i)
        misses.push(cppUi[i] + ': wanted ' + i + ', parameter got ' + (got.last ? got.last.value : 'nothing')
                    + ' (norm ' + (got.last ? got.last.norm.toFixed(5) : '—') + ')');
    }
    await pg.evaluate(() => window.__wmClose());
    chk(misses.length === 0, 'ROUND TRIP — ' + param + ': every live mode reaches the PARAMETER at its C++ index',
        misses.length ? (live.length - misses.length) + '/' + live.length + ' ok · ' + misses.slice(0,3).join(' · ')
                        + (misses.length>3 ? ' …+'+(misses.length-3) : '')
                      : live.length + '/' + live.length + (isPill ? ' (WARP 2 pill)' : ' (WARP knob)'));
  }

  // ── 4  THE GUARD REDS ──────────────────────────────────────────────────────────────────
  {
    const boot = await pg.evaluate(() => window.__warpGuard({ quiet: true }));
    chk(boot.ok === (MUT === 0 || MUT === 3), 'GUARD — the shipped page boots GREEN (and a broken list does not)',
        'ok=' + boot.ok + (boot.problems.length ? '  · ' + boot.problems.slice(0,2).join(' · ') : '') + '  seen=' + JSON.stringify(boot.seen));
    const red = await pg.evaluate(() => {
      const st = window.__stubState('SYN_OSC_B_WARP_MODE');
      st.properties = Object.assign({}, st.properties, { numSteps: 11, end: 10 });   // the parameter shrank behind the list's back
      const g = window.__warpGuard();
      const banner = !!document.getElementById('warp-desync-banner');
      window.__wmClose();
      const opened = window.__wmOpen('#syn-panel [data-syn="SYN_OSC_A_WARP_AMOUNT"]', 300, 220);
      const before = window.__emits.length;
      const refused = (opened !== 'browser') && (window.__emits.length === before);
      st.properties = Object.assign({}, st.properties, { numSteps: 48, end: 47 });
      const g2 = window.__warpGuard();
      return { g: g, banner: banner, opened: opened, refused: refused, recovered: g2.ok,
               bannerGone: !document.getElementById('warp-desync-banner') };
    });
    chk(red.g.ok === false && red.g.problems.length > 0, 'GUARD — a deliberate desync FAILS the guard', red.g.problems[0] || '');
    chk(red.banner, 'GUARD — it is LOUD: the red banner lands on the page', 'banner=' + red.banner);
    chk(red.refused, 'GUARD — the picker REFUSES to open while desynced (silence beats a wrong mode)', 'open attempt → ' + red.opened);
    chk(red.recovered && red.bannerGone, 'GUARD — it clears when the two agree again', 'ok=' + red.recovered + ' banner gone=' + red.bannerGone);
  }

  // ── 5  UNKNOWABLE ≠ WRONG ──────────────────────────────────────────────────────────────
  {
    const r = await pg.evaluate(() => {
      const st = window.__stubState('SYN_OSC_C_WARP_MODE');
      const keep = st.properties;
      st.properties = { start:0, end:1, skew:1, interval:0, numSteps:100, name:'', label:'', parameterIndex:-1 };
      const card = window.__paramCardinality('SYN_OSC_C_WARP_MODE');
      const g = window.__warpGuard({ quiet: true });
      st.properties = keep;
      return { card: card, ok: g.ok, problems: g.problems };
    });
    chk(r.card === 0, 'UNKNOWABLE — a param whose properties have not arrived reports cardinality 0, never 100', 'card=' + r.card);
    chk(r.ok === (MUT === 0 || MUT === 3), 'UNKNOWABLE — the guard SKIPS it instead of guessing', 'ok=' + r.ok + (r.problems[0] ? ' · ' + r.problems[0] : ''));
  }

  // ── 6  NO REGRESSION ───────────────────────────────────────────────────────────────────
  {
    const sample = await pg.evaluate(() => {
      window.__wmClose();
      const dev = document.querySelector('#osc-a-warp2-val') && document.querySelector('#osc-a-warp2-val').closest('.device.osc');
      if (dev) dev.classList.add('engine-sample');
      const o = window.__wmOpen('#osc-a-warp2-val', 300, 220);
      const m = document.getElementById('syn-ctx-menu');
      const rows = m ? [...m.querySelectorAll('.syn-ctx-item')].map((d) => d.textContent) : [];
      const hdr = m ? (m.querySelector('.syn-ctx-header') || {}).textContent : '';
      window.__wmClose(); if (dev) dev.classList.remove('engine-sample');
      return { o: o, rows: rows, hdr: hdr };
    });
    chk(sample.o === 'ctxmenu' && sample.rows.length === 6,
        'NO REGRESSION — the 6-entry SAMPLE warp list still uses the flat house menu', sample.hdr + ' → [' + sample.rows.join('|') + ']');
    const uniq = [...new Set(errs)];
    const novel = baseErrs ? uniq.filter((e) => baseErrs.indexOf(e) < 0) : uniq;
    chk(baseErrs !== null && novel.length === 0, 'NO REGRESSION — no page error the pre-change page did not already throw',
        baseErrs === null ? 'WARPM_PREPAGE not set/found — no floor, cannot compare  (this page threw: ' + (uniq[0] || 'nothing') + ')'
                          : uniq.length + ' error(s), ' + novel.length + ' novel' + (novel.length ? ': ' + novel[0] : '')
                            + '   [floor: ' + baseErrs.length + ' pre-existing, e.g. ' + (baseErrs[0] || '—') + ']');
    const unexpected = cons.filter((c) => !/WARP DESYNC|CHOICE DESYNC/.test(c));
    chk(unexpected.length === 0, 'NO REGRESSION — no unexpected console errors',
        unexpected.length ? unexpected.slice(0,2).join(' · ') : (cons.length ? cons.length + ' expected desync errors only' : 'clean'));
  }

  console.log('\n  PASS ' + pass + '   FAIL ' + fail + '\n');
  await b.close();
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
