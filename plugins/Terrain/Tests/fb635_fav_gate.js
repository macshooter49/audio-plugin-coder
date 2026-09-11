// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb635_fav_gate.js — fb635: THE HEART IS REMEMBERED.
//
//    node Tests/fb635_fav_gate.js             # from plugins/Terrain (NODE_PATH=Tests/node_modules)
//
//  Max: "every time I try to save a favourite preset and I load up a new instance in a project, it doesn't save. So my heart
//  is gone ... especially for people on their custom computers ... Terrain remembers that you favourited this."
//
//  THE MECHANISM. tw::bank::setFavourite writes <userRoot>/favourites.json as {"keys":["Bank/Name", …]} and every heart WAS
//  saved — but boot assigned that object straight to S.favs, a MAP keyed by "Bank/Name", so no key matched and a new instance
//  drew every heart dark (the stub of every older gate answered '{}', so none ever read the real format back).
//
//  THE BARS (the stub keeps the favourites file exactly as the C++ does; a "new instance" = a fresh page booted from it)
//   1  BOOT READS THE FILE — {"keys":["User/Slatt","Terra/Glacier"]}: those two rows wear a lit heart, the others do not
//   2  A NEW INSTANCE REMEMBERS — a heart clicked in one page is lit in a fresh page booted from the file it wrote
//   3  A RENAME CARRIES THE HEART — Slatt renamed to "Slatt Two": the file says "User/Slatt Two", not "User/Slatt", and the
//      renamed row is lit
//   4  ANOTHER INSTANCE'S HEART APPEARS — the file gains "Terra/Cirrus" behind the page's back; reopening the browser lights it
//
//  MUTATION CONTROLS
//    FV_MUT=rawread   boot assigns the file object to the map again (the fb634 page)   → [1] [2] [3] [4] RED
//    FV_MUT=nokey     a rename does not re-key the heart                                → [3] RED
//    FV_MUT=nopull    the file is read only at boot, never again                        → [4] RED
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require('path');
const fs = require('fs'), os = require('os');
const puppeteer = require(require('path').join(__dirname, 'node_modules', 'puppeteer-core'));
const PAGE0 = path.join(__dirname, '..', 'Source', 'ui', 'public', 'index.html');
const MUT = process.env.FV_MUT || '';
let pass = 0, fail = 0;
const gate = (ok, name, detail) => { ok ? ++pass : ++fail; console.log(`  ${ok ? 'PASS' : 'FAIL'}  ${name}\n        ${detail}`); };
function page () {
  if (! MUT) return PAGE0;
  let s = fs.readFileSync(PAGE0, 'utf8');
  const sub = (a, b) => { const n = s.split(a).length - 1; if (n !== 1) { console.log('  MUTATION anchor matched ' + n + 'x: ' + a.slice(0, 70)); process.exit(2); } s = s.replace(a, b); };
  if (MUT === 'rawread') sub(".then(j => { if (j != null) S.favs = favsFrom(j); })", ".then(j => { try { const o = JSON.parse(j || 'null'); if (o && typeof o === 'object') S.favs = o; } catch (e) {} })");
  if (MUT === 'nokey')   sub("function refav(oldBank, oldName, newBank, newName) {", "function refav(oldBank, oldName, newBank, newName) { return;");
  if (MUT === 'nopull')  sub("function pullFavs() { return", "var __pf0 = 0; function pullFavs() { if (__pf0++) return Promise.resolve(); return");
  const f = path.join(os.tmpdir(), 'fb635_fav_' + MUT + '.html'); fs.writeFileSync(f, s); return f;
}
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
    /* fb635 — the favourites FILE, exactly as tw::bank writes it: {"keys":["Bank/Name", …]} */
    if (n === 'getFavourites') return r (window.__FAVFILE || 'null');
    if (n === 'setFavourite') { const k = String (a[0]).trim () + '/' + String (a[1]).trim (); let o = null; try { o = JSON.parse (window.__FAVFILE || 'null'); } catch (e) {}
      const keys = (o && Array.isArray (o.keys)) ? o.keys.filter (x => x !== k) : []; if (String (a[2]) === 'true') keys.push (k);
      window.__FAVFILE = JSON.stringify ({ keys }); return r ('ok'); }
    if (n === 'renamePresetFile') { for (const b of window.__CAT.banks) for (const p of b.presets) if (p.path === String (a[0])) { p.name = String (a[1]); p.path = (b.dir || '/tmp') + '/' + p.name + '.terrain'; } return r ('ok'); }
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
  console.log('══ fb635 FAV GATE — the heart is remembered ══   mutation: ' + (MUT || '(none)'));
  const PAGE = page();
  const b = await puppeteer.launch({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const wait = ms => new Promise(r => setTimeout(r, ms));
  const errors = [];
  async function instance (favFile) {   // one plugin instance = one fresh page, booted from the favourites file on disk
    const p = await b.newPage(); await p.setViewport({ width: 820, height: 656, deviceScaleFactor: 1 });
    p.on('pageerror', e => errors.push(String(e.message || e).slice(0, 140)));
    await p.evaluateOnNewDocument((f) => { window.__FAVFILE = f; }, favFile);
    await p.evaluateOnNewDocument(STUB, '');
    await p.goto('file://' + PAGE, { waitUntil: 'load', timeout: 60000 }); await wait(1600);
    await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark'); const sp = document.getElementById('syn-panel'); if (sp) { sp.classList.remove('hidden'); sp.style.display = 'block'; } window.dispatchEvent(new Event('resize')); });
    await wait(1600); return p;
  }
  const openB = async (p) => { if (! await p.evaluate(() => document.getElementById('tp-b').classList.contains('on'))) { await p.click('#preset-name'); await wait(160); await p.click('#tp-q-browse'); await wait(500); } };
  const closeB = async (p) => { await p.evaluate(() => { try { document.getElementById('tp-close').click(); } catch (e) {} }); await wait(200); };
  const lit = (p) => p.evaluate(() => [...document.querySelectorAll('#tp-rows .tp-row')].filter(r => r.querySelector('.c-fv.on')).map(r => r.querySelector('.c-nm .t').textContent.trim()).sort());
  const has = (list, name) => list.some(t => t === name || t.endsWith(' ' + name) || t.endsWith(name));
  const file = (p) => p.evaluate(() => window.__FAVFILE);
  // [1]
  const p1 = await instance('{"keys":["User/Slatt","Terra/Glacier"]}');
  await openB(p1); const l1 = await lit(p1);
  gate(l1.length === 2 && has(l1, 'Slatt') && has(l1, 'Glacier'), '[1] BOOT READS THE FILE — the two saved hearts are lit, nothing else', `lit: ${l1.join(' · ') || 'none'}`);
  // [2] click Monte Cristo's heart, then a NEW instance from the file it wrote
  await p1.evaluate(() => { const r = [...document.querySelectorAll('#tp-rows .tp-row')].find(x => x.querySelector('.c-nm .t').textContent.trim().endsWith('Monte Cristo')); if (r) r.querySelector('.c-fv').click(); }); await wait(250);
  const f1 = await file(p1); await p1.close();
  const p2 = await instance(f1); await openB(p2); const l2 = await lit(p2);
  gate(l2.length === 3 && has(l2, 'Slatt') && has(l2, 'Glacier') && has(l2, 'Monte Cristo'), '[2] A NEW INSTANCE REMEMBERS — the heart clicked in one instance is lit in a fresh one booted from the file',
    `file after the click ${f1} · new instance lit: ${l2.join(' · ') || 'none'}`);
  // [3] rename Slatt → "Slatt Two" through the real sheet
  const opened = await p2.evaluate(() => { const S = window.__tiPresets.state; const id = S.presets.findIndex(p => p.name === 'Slatt'); if (id < 0) return false; window.__tiPresets.askRename(id); return true; });
  if (opened) { await wait(200); await p2.evaluate(() => { document.getElementById('tp-rn').value = 'Slatt Two'; document.getElementById('tp-sh-ok').click(); }); await wait(700); }
  const f3 = await file(p2), l3 = await lit(p2); let k3 = []; try { k3 = JSON.parse(f3).keys; } catch (e) {}
  gate(opened && k3.includes('User/Slatt Two') && ! k3.includes('User/Slatt') && has(l3, 'Slatt Two'), '[3] A RENAME CARRIES THE HEART — the file re-keys to the new name and the renamed row is lit',
    `file ${f3} · lit: ${l3.join(' · ') || 'none'}`);
  // [4] another instance writes a heart; reopening the browser shows it
  await p2.evaluate(() => { let o = {}; try { o = JSON.parse(window.__FAVFILE); } catch (e) {} (o.keys = o.keys || []).push('Terra/Cirrus'); window.__FAVFILE = JSON.stringify(o); });
  await closeB(p2); await openB(p2); const l4 = await lit(p2);
  gate(has(l4, 'Cirrus'), "[4] ANOTHER INSTANCE'S HEART APPEARS — reopening the browser re-reads the file", `lit: ${l4.join(' · ') || 'none'}`);
  if (errors.length) console.log('  page errors: ' + [...new Set(errors)].slice(0, 2).join(' | '));
  console.log(`\n  ${pass} passed, ${fail} FAILED`); await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.log('  CRASH ' + (e && e.stack || e)); process.exit(2); });
