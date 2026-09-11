// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb635_page_gate.js — fb635: THE SHAPE MENU FITS, CUSTOM WEARS THE PEN, THE INSPECTOR SCROLLS, NO ⌘S.
//
//    node Tests/fb635_page_gate.js            # from plugins/Terrain (NODE_PATH=Tests/node_modules)
//    FB635_SHOT=/dir  node …                  # also writes /dir/fb635-lfo-menu.png at 2×
//
//  Max: "I have to scroll in order to get to Chaos and Rossler. Could you make it to where I don't have to
//  scroll anymore ... make sure it doesn't overlap or get cut out anywhere." / "change that custom emblem
//  ... I do not like that custom emblem when it comes to selecting LFOs." / "The information section won't
//  let me scroll ... it's probably because I have my mouse hovering over the styles and the types." /
//  "take away that Control S setting for saving presets ... I use FL Studio ... I don't want to Ctrl-S and
//  accidentally override a preset."
//
//  THE BARS
//   1  THE SHAPE MENU HAS NO SCROLL — the real menu (click the LFO's shape pill): all eleven rows, the
//      glass is as tall as its rows (scrollHeight ≤ clientHeight), fully on screen, Lorenz and Rossler
//      are the last two rows and inside the glass
//   2  CUSTOM WEARS THE PEN — the Custom row's emblem is the vector-edit mark (a hollow node + handles),
//      not the tab's own points (which drew a second Saw)
//   3  THE WHEEL OVER A CHIP SCROLLS THE INSPECTOR — a real wheel (CDP) over a Style chip moves the
//      inspector's scroller; the event is not cancelled
//   4  NO SAVE SHORTCUT — Cmd+S and Ctrl+S reach the page and do nothing: no save native, no sheet, the
//      event is not cancelled (the host keeps its own Ctrl+S); the Save menu row shows no ⌘S
//
//  MUTATION CONTROLS (each reddens exactly its own bar)
//    PG_MUT=scroll   the shape menu opens without `fit` (the 228 px cap)          → [1] RED
//    PG_MUT=emblem   Custom's emblem goes back to the tab's own points            → [2] RED
//    PG_MUT=wheel    the strips' vertical→sideways wheel hijack comes back        → [3] RED
//    PG_MUT=cmds     the Cmd/Ctrl+S keydown handler comes back                    → [4] RED
// ══════════════════════════════════════════════════════════════════════════════════════════════
const path = require('path');
const fs = require('fs'), os = require('os');
const puppeteer = require(require('path').join(__dirname, 'node_modules', 'puppeteer-core'));
const PAGE0 = path.join(__dirname, '..', 'Source', 'ui', 'public', 'index.html');
const MUT = process.env.PG_MUT || '';
let pass = 0, fail = 0;
const gate = (ok, name, detail) => { ok ? ++pass : ++fail; console.log(`  ${ok ? 'PASS' : 'FAIL'}  ${name}\n        ${detail}`); };

function page () {
  if (! MUT) return PAGE0;
  let s = fs.readFileSync(PAGE0, 'utf8');
  const sub = (a, b) => { const n = s.split(a).length - 1; if (n !== 1) { console.log('  MUTATION anchor matched ' + n + ' times: ' + a.slice(0, 80)); process.exit(2); } s = s.replace(a, b); };
  if (MUT === 'scroll') sub("function(i){ pickShape(i); }, true); };", "function(i){ pickShape(i); }); };");
  if (MUT === 'emblem') sub("if(k===7) return ICON_CUSTOM; ", "");
  if (MUT === 'wheel')  sub("    st.addEventListener('scroll', () => stripEdge(st));",
      "    st.addEventListener('wheel', e => { if (Math.abs(e.deltaX) < Math.abs(e.deltaY)) { st.scrollLeft += e.deltaY; e.preventDefault(); } }, { passive: false });\n    st.addEventListener('scroll', () => stripEdge(st));");
  if (MUT === 'cmds')   sub("  /* fb635 — NO SAVE SHORTCUT.",
      "  document.addEventListener('keydown', e => { if ((e.metaKey || e.ctrlKey) && !e.altKey && String(e.key).toLowerCase() === 's') { e.preventDefault(); if (e.shiftKey) openSave(); else saveCurrent(); } });\n  /* fb635 — NO SAVE SHORTCUT.");
  const f = path.join(os.tmpdir(), 'fb635_mut_' + MUT + '.html'); fs.writeFileSync(f, s); return f;
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
      Object.assign(P('Terra', 'Glacier', 'Pad', 'Glassy,Evolving', [2,0,0,0,2,0], true), { note: 'Mod wheel opens the filter. '.repeat(40) }), P('Terra', 'Cirrus', 'Pad', 'Bright,Wide', [1,0,0,0,1,0], true),
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
  console.log('══ fb635 PAGE GATE — the shape menu, the pen, the inspector wheel, no ⌘S ══   mutation: ' + (MUT || '(none)'));
  const PAGE = page();
  const b = await puppeteer.launch({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const p = await b.newPage();
  await p.setViewport({ width: 820, height: 656, deviceScaleFactor: process.env.FB635_SHOT ? 2 : 1 });
  const errors = []; p.on('pageerror', e => errors.push(String(e.message || e).slice(0, 160)));
  await p.evaluateOnNewDocument(STUB, MUT);
  await p.goto('file://' + PAGE, { waitUntil: 'load', timeout: 60000 });
  await new Promise(r => setTimeout(r, 1600));
  await p.evaluate(() => { document.documentElement.setAttribute('data-theme', 'dark'); document.body.classList.add('ti-syn-open'); const sp = document.getElementById('syn-panel'); if (sp) { sp.classList.remove('hidden'); sp.style.display = 'block'; } window.dispatchEvent(new Event('resize')); });
  await new Promise(r => setTimeout(r, 2000));
  const wait = ms => new Promise(r => setTimeout(r, ms));

  // ── [1] [2] the LFO shape menu ───────────────────────────────────────────────────────────
  const m = await p.evaluate(() => {
    const sb = document.querySelector('#mod-engine #mv-shape'); if (! sb) return { err: 'no shape pill in #mod-engine' };
    sb.click(); const mn = document.querySelector('.mv-menu.open'); if (! mn) return { err: 'the shape menu did not open' };
    const r = mn.getBoundingClientRect();
    const rows = [...mn.querySelectorAll('div[data-i]')].map(d => { const q = d.getBoundingClientRect(); return { t: d.textContent.trim(), top: q.top, bot: q.bottom, svg: (d.querySelector('svg') || {}).outerHTML || '' }; });
    return { top: r.top, bot: r.bottom, sh: mn.scrollHeight, ch: mn.clientHeight, vh: innerHeight, rows };
  });
  if (process.env.FB635_SHOT) { try { fs.mkdirSync(process.env.FB635_SHOT, { recursive: true }); await p.screenshot({ path: path.join(process.env.FB635_SHOT, 'fb635-lfo-menu.png') }); } catch (e) {} }
  if (m.err) { gate(false, '[1] THE SHAPE MENU HAS NO SCROLL', m.err); gate(false, '[2] CUSTOM WEARS THE PEN', m.err); }
  else {
    const n = m.rows.length, last2 = m.rows.slice(-2);
    const inside = q => q.top >= m.top - 0.5 && q.bot <= m.bot + 0.5 && q.bot <= m.top + m.ch + 6;
    const ok1 = n === 11 && m.sh <= m.ch + 1 && m.top >= 0 && m.bot <= m.vh && last2.map(q => q.t).join(',') === 'Lorenz,Rossler' && last2.every(inside);
    gate(ok1, '[1] THE SHAPE MENU HAS NO SCROLL — eleven rows, the glass as tall as its rows, on screen; Lorenz and Rossler last and visible',
      `${n} rows · content ${m.sh} vs glass ${m.ch} · glass ${Math.round(m.top)}..${Math.round(m.bot)} of ${m.vh} · last two ${last2.map(q => q.t + ' ' + Math.round(q.top) + '..' + Math.round(q.bot)).join(', ')}`);
    const cu = m.rows.find(q => q.t === 'Custom') || {}, saw = m.rows.find(q => q.t === 'Saw') || {};
    const pen = /<rect\b/.test(cu.svg || '') && (cu.svg.match(/<circle\b/g) || []).length === 2 && cu.svg !== saw.svg;
    gate(pen, '[2] CUSTOM WEARS THE PEN — a hollow node and two handles, not the tab\'s own points',
      pen ? 'the Custom row draws the vector-edit mark' : 'the Custom row draws: ' + (cu.svg || '(none)').slice(0, 140));
  }
  await p.mouse.click(5, 640); await wait(120);

  // ── [3] the inspector wheel over a chip ──────────────────────────────────────────────────
  await p.click('#preset-name'); await wait(160); await p.click('#tp-q-browse'); await wait(400);
  const setup = await p.evaluate(() => {
    const chip = document.querySelector('#tp-chips-style .tp-chip') || document.querySelector('#tp-chips-type .tp-chip');
    if (! chip) return { err: 'no chip in the inspector' };
    let sc = chip.parentElement; while (sc && ! (sc.scrollHeight > sc.clientHeight + 4 && /(auto|scroll)/.test(getComputedStyle(sc).overflowY))) sc = sc.parentElement;
    if (! sc) return { err: 'the inspector does not overflow (nothing to scroll)' };
    chip.scrollIntoView({ block: 'center' });
    const q = chip.getBoundingClientRect(), s0 = sc.scrollTop, max = sc.scrollHeight - sc.clientHeight;
    window.__wheelPrevented = null; document.addEventListener('wheel', e => { window.__wheelPrevented = e.defaultPrevented; }, { once: true });
    return { x: q.left + q.width / 2, y: q.top + q.height / 2, s0, max, sc: sc.className || sc.id };
  });
  if (setup.err) gate(false, '[3] THE WHEEL OVER A CHIP SCROLLS THE INSPECTOR', setup.err);
  else {
    const down = setup.s0 < setup.max / 2, dy = down ? 160 : -160;
    await p.mouse.move(setup.x, setup.y); await wait(60); await p.mouse.wheel({ deltaY: dy }); await wait(350);
    const after = await p.evaluate(() => { const chip = document.querySelector('#tp-chips-style .tp-chip') || document.querySelector('#tp-chips-type .tp-chip');
      let sc = chip.parentElement; while (sc && ! (sc.scrollHeight > sc.clientHeight + 4 && /(auto|scroll)/.test(getComputedStyle(sc).overflowY))) sc = sc.parentElement;
      return { s1: sc ? sc.scrollTop : -1, prevented: window.__wheelPrevented }; });
    const moved = down ? after.s1 > setup.s0 + 20 : after.s1 < setup.s0 - 20;
    gate(moved && after.prevented !== true, '[3] THE WHEEL OVER A CHIP SCROLLS THE INSPECTOR — a real wheel over a Style chip moves .' + setup.sc,
      `scrollTop ${Math.round(setup.s0)} → ${Math.round(after.s1)} (wheel ${dy > 0 ? 'down' : 'up'}, range 0..${Math.round(setup.max)}) · cancelled: ${after.prevented}`);
  }
  await p.keyboard.press('Escape'); await wait(120);
  await p.evaluate(() => { try { document.getElementById('tp-close').click(); } catch (e) {} }); await wait(160);

  // ── [4] no save shortcut ─────────────────────────────────────────────────────────────────
  const k = await p.evaluate(() => {
    const n0 = window.__gateCalls.length, fire = (o) => { const ev = new KeyboardEvent('keydown', Object.assign({ key: 's', code: 'KeyS', bubbles: true, cancelable: true }, o)); return document.dispatchEvent(ev) ? 'passed' : 'cancelled'; };
    const r = { cmd: fire({ metaKey: true }), ctrl: fire({ ctrlKey: true }), cmdShift: fire({ metaKey: true, shiftKey: true, key: 'S' }) };
    const sh = document.getElementById('tp-sheet'); r.sheet = !! (sh && sh.classList.contains('on'));
    r.saves = window.__gateCalls.slice(n0).filter(c => /save/i.test(c.fn)).map(c => c.fn);
    return r; });
  await p.keyboard.press('Escape'); await wait(100);
  await p.click('#preset-save-btn'); await wait(160);
  const menuTxt = await p.evaluate(() => { const c = document.getElementById('tp-ctx'); return c ? c.textContent.replace(/\s+/g, ' ').trim() : '(no menu)'; });
  const ok4 = k.cmd === 'passed' && k.ctrl === 'passed' && k.cmdShift === 'passed' && ! k.sheet && ! k.saves.length && ! /⌘/.test(menuTxt);
  gate(ok4, '[4] NO SAVE SHORTCUT — Cmd+S / Ctrl+S / Cmd+Shift+S do nothing and are not cancelled; the Save row has no ⌘S',
    `Cmd+S ${k.cmd} · Ctrl+S ${k.ctrl} · Cmd+Shift+S ${k.cmdShift} · sheet opened: ${k.sheet} · save natives: ${k.saves.join(',') || 'none'} · menu "${menuTxt.slice(0, 60)}"`);

  if (errors.length) console.log('  page errors: ' + errors.join(' | '));
  console.log(`\n  ${pass} passed, ${fail} FAILED`);
  await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.log('  CRASH ' + (e && e.stack || e)); process.exit(2); });
