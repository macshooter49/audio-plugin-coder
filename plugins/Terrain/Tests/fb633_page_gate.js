// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb633_page_gate.js — fb633: THE ADD-EFFECT MENU IS A–Z, THE SAVE SHEET IS WIDE.
//
//    node Tests/fb633_page_gate.js            # from plugins/Terrain (NODE_PATH=Tests/node_modules)
//    FB633_SHOT=/dir  node …                  # also writes /dir/fb633-sheet.png (the Save sheet at 2×)
//
//  Max: "whenever we press add effect I want the effects categorized by name, A through Z.
//  Compressor at C, Granular at G. Every time I'm trying to add an effect I be getting confused."
//  And: "make the save presets menu wider — not lengthier but wider, not edge to edge — so our
//  styles and types could be wide instead of clamped together."
//
//  THE BARS
//   1  THE ADD-EFFECT MENU IS A–Z — the real menu (window.__fxAddMenu) read row by row: every row
//      follows the one before it alphabetically; Compress sits before Granular
//   2  THE SAVE SHEET IS WIDE — the real sheet (+ → Save as…): the card is 480 wide, wears .wide,
//      the Style chips have the room; a sheet without .wide is 340 (the other sheets)
//
//  MUTATION CONTROLS
//    PG_MUT=order    bar [1] expects the rows in the order the cores were BUILT (the old menu) → RED
//    PG_MUT=narrow   bar [2] expects the old 340 card → RED
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require('path');
const fs = require('fs');
const puppeteer = require(require('path').join(__dirname, 'node_modules', 'puppeteer-core'));
const PAGE = path.join(__dirname, '..', 'Source', 'ui', 'public', 'index.html');
const MUT = process.env.PG_MUT || '';
let pass = 0, fail = 0;
const gate = (ok, name, detail) => { ok ? ++pass : ++fail; console.log(`  ${ok ? 'PASS' : 'FAIL'}  ${name}\n        ${detail}`); };

const STUB = (MUT) => {
  window.__gateCalls = []; window.__panels = []; window.__log = [];
  const mk = () => ({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},
    getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
  const P = (bank, name, type, styles, c, factory) => ({ name, bank, author: factory ? 'Waves Crate' : 'Max', type, styles, note: factory ? 'Mod wheel opens the filter.' : '', fv: 1,
    carries: { wt: c[0], smp: c[1], ir: c[2], flow: c[3], lfo: c[4], nodes: c[5] }, path: '/tmp/Banks/' + bank + '/' + name + '.terrain', mtime: 1, factory: !!factory });
  window.__CAT = { banks: [
    { name: 'Terra', author: 'Waves Crate', version: '1', factory: true, dir: '/f/Terra', presets: [
      P('Terra', 'Glacier', 'Pad', 'Glassy,Evolving', [2,0,0,0,2,0], true), P('Terra', 'Cirrus', 'Pad', 'Bright,Wide', [1,0,0,0,1,0], true),
      P('Terra', 'Tectonic', 'Bass', 'Dark,Punchy', [1,0,0,1,0,0], true), P('Terra', 'Anvil', 'Lead', 'Metallic,Punchy', [1,0,0,2,0,0], true) ] },
    { name: 'User', author: 'Max', version: '1', factory: false, dir: '/tmp/Banks/User', presets: [
      P('User', 'Slatt', 'Bass', 'Dark,Dirty', [1,0,0,1,0,0], false), P('User', 'Monte Cristo', 'Keys', 'Warm,Clean', [1,0,0,0,0,0], false) ] } ],
    caps: { banks: 2, presets: 6, unreadable: 0, maxBanks: 200, maxPresets: 5000 }, userRoot: '/tmp/Banks', factoryRoot: '/f' };
  let curMeta = { name: 'Glacier', bank: 'Terra', author: 'Waves Crate', type: 'Pad', styles: 'Glassy,Evolving', note: 'Mod wheel opens the filter.', fv: 1 };
  const nf = (n) => (...a) => new Promise (r => {
    window.__gateCalls.push ({ fn: n, args: a.map (String) });
    if (n === 'editArm') { if (false && +a[0] === 1) { const ae = document.activeElement; if (ae && ae !== document.body) { window.__log.push('editArm(1) steals focus from ' + (ae.id || ae.className)); ae.blur(); } } return r('ok'); }
    if (n === 'listPresets')   return r (JSON.stringify (window.__CAT));
    if (n === 'getPresetMeta') return r (JSON.stringify (curMeta));
    if (n === 'setPresetMeta') { try { curMeta = JSON.parse (a[0]); } catch (e) {} return r ('ok'); }
    if (n === 'savePresetToBank') { let m = {}; try { m = JSON.parse (a[1]); } catch (e) {} const bank = String (a[0]);
      let b = window.__CAT.banks.find (x => x.name === bank); if (! b) { b = { name: bank, author: m.author || '', version: '1', factory: false, dir: '/tmp/Banks/' + bank, presets: [] }; window.__CAT.banks.push (b); }
      const row = P (bank, m.name, m.type, m.styles, [0,0,0,0,0,0], false); row.author = m.author; row.note = m.note || '';
      const at = b.presets.findIndex (x => x.name === m.name); if (at >= 0) b.presets[at] = row; else b.presets.push (row);
      curMeta = Object.assign ({}, m, { bank }); return r (row.path); }
    if (n === 'updatePresetMeta') { let m = {}; try { m = JSON.parse (a[1]); } catch (e) {} for (const b of window.__CAT.banks) for (const p of b.presets) if (p.path === String(a[0])) Object.assign(p, m); return r('ok'); }
    if (n === 'getFavourites') return r ('{}');
    if (n === 'getVocab')      return r ('');
    if (/^(setFavourite|setVocab|createBank|renameBank|deleteBank|exportBank|exportPreset|exportPresetFile|importPack)$/.test (n)) return r ('ok');
    if (/getPresets/i.test (n)) return r ('[]');
    if (/Json|JSON|Names|Lanes|Shapes|Envs|Mod$/.test (n)) return r ('');
    r (0); });
  window.Juce = { getSliderState: mk, getToggleState: mk, getComboBoxState: mk, getNativeFunction: nf,
    backend: { addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,
    get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',
    __juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}};
  window.prompt = () => { window.__panels.push ('prompt'); return null; };
  window.confirm = () => { window.__panels.push ('confirm'); return false; };
  window.alert = () => { window.__panels.push ('alert'); };
};

(async () => {
  console.log('══ fb633 PAGE GATE — the A–Z menu, the wide sheet ══   mutation: ' + (MUT || '(none)'));
  const b = await puppeteer.launch({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const p = await b.newPage();
  await p.setViewport({ width: 820, height: 656, deviceScaleFactor: process.env.FB633_SHOT ? 2 : 1 });
  const errors = []; p.on('pageerror', e => errors.push(String(e.message || e).slice(0, 160)));
  await p.evaluateOnNewDocument(STUB, MUT);
  await p.goto('file://' + PAGE, { waitUntil: 'load', timeout: 60000 });
  await new Promise(r => setTimeout(r, 1600));
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark'); document.body.classList.add('ti-syn-open'); const sp = document.getElementById('syn-panel'); if (sp) { sp.classList.remove('hidden'); sp.style.display = 'block'; } window.dispatchEvent(new Event('resize')); });
  await new Promise(r => setTimeout(r, 2000));
  const wait = ms => new Promise(r => setTimeout(r, ms));

  // ── [1] the add-effect menu ──────────────────────────────────────────────────────────────
  const rows = await p.evaluate(() => {
    try { window.__fxAddMenu(300, 300); } catch (e) { return { err: String(e) }; }
    const items = [...document.querySelectorAll('#syn-panel .syn-ctx-menu .syn-ctx-item')].map(e => e.textContent.trim());
    try { window.__synHideMenu && window.__synHideMenu(); } catch (e) {}
    return { items };
  });
  const names = (rows.items || []).map(t => t.replace(/\s+\(max\)$/, '').replace(/\s+×\d+$/, '').trim());
  const sorted = names.slice().sort((a, c) => a.localeCompare(c, 'en', { sensitivity: 'base' }));
  const isAZ = names.length >= 8 && names.every((n, i) => n === sorted[i]);
  const iC = names.indexOf('Compress'), iG = names.indexOf('Granular');
  const wantAZ = MUT !== 'order';
  gate(rows.err == null && (isAZ === wantAZ) && (wantAZ ? (iC >= 0 && iG >= 0 && iC < iG) : true),
    '[1] THE ADD-EFFECT MENU IS A–Z — every row follows the one before it; Compress before Granular',
    (rows.err ? rows.err : names.join(' · ')) + (wantAZ ? '' : '   (control: expects the build order)'));

  // ── [2] the save sheet ───────────────────────────────────────────────────────────────────
  await p.click('#preset-save-btn'); await wait(150);
  await p.evaluate(() => { const r = document.querySelector('#tp-ctx .pi[data-a=saveas]'); if (r) r.click(); }); await wait(250);
  const m = await p.evaluate(() => {
    const sh = document.getElementById('tp-sheet'), card = sh && sh.querySelector('.tp-card');
    if (!sh || !card || !sh.classList.contains('on')) return { on: false };
    const cw = card.getBoundingClientRect().width, panel = sh.getBoundingClientRect().width;
    const chp = document.getElementById('tp-sv-style'); const chw = chp ? chp.getBoundingClientRect().width : 0;
    const wide = card.classList.contains('wide');
    card.classList.remove('wide'); const base = card.getBoundingClientRect().width; card.classList.add('wide');
    return { on: true, cw, panel, chw, wide, base, edge: panel - cw };
  });
  if (process.env.FB633_SHOT) { try { fs.mkdirSync(process.env.FB633_SHOT, { recursive: true }); await p.screenshot({ path: path.join(process.env.FB633_SHOT, 'fb633-sheet.png') }); } catch (e) {} }
  const wantW = MUT === 'narrow' ? 340 : 480;
  gate(m.on && Math.round(m.cw) === wantW && m.wide && Math.round(m.base) === 340 && m.chw >= (wantW - 120) && m.edge >= 80,
    '[2] THE SAVE SHEET IS WIDE — 480 with .wide, the Style chips have the room, never edge to edge; a plain sheet is 340',
    m.on ? `card ${Math.round(m.cw)} (want ${wantW}) wide=${m.wide} · chips ${Math.round(m.chw)} · plain ${Math.round(m.base)} · clear of the edges ${Math.round(m.edge)}` : 'the sheet did not open');
  await p.keyboard.press('Escape'); await wait(100);

  if (errors.length) console.log('  page errors: ' + errors.join(' | '));
  console.log(`\n  ${pass} passed, ${fail} FAILED`);
  await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.log('  CRASH ' + (e && e.stack || e)); process.exit(2); });
