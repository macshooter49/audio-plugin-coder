// fb606_wtnav_gate — THE MASTER FOLDER NAVIGATES, AND THE FOLDER MENU IS ON TOP.
//
//   node fb606_wtnav_gate.js [page.html]      (viewport = the shipped 820 x 656)
//
// BARS
//  1  RECURSIVE PAYLOAD → A TREE. A registered MASTER folder whose files live one and two levels
//     down renders ONE category whose count is EVERY table beneath it (the owner's "open the
//     MASTER FOLDER and see all of the tables in the SUB FOLDERS too"), carrying a chevron.
//     The stub sends NO `rel` key at all — only paths — so this also proves the derived path.
//  2  ONE CLICK DESCENDS, AND THE LEVEL OPENS ON ALL. Left column becomes All + the subfolders;
//     the right pane shows everything beneath; the breadcrumb names where you are.
//  3  DOUBLE-CLICK DESCENDS ONCE, NOT TWICE (the 350 ms guard).
//  4  BACK / FORWARD. The header arrows walk the history, both directions, to the pixel-identical
//     panel (FIXED POSITIONS: the panel box and the pane boxes never move or resize).
//  5  CENTRELINE. The two arrow glyph boxes share the headphone's centre-Y, and disabling one
//     moves nothing.
//  6  SEARCH SPANS THE SUBTREE. Typed three folders deep, a query finds a table in a DIFFERENT
//     branch — exactly once.
//  7  🚨 THE FOLDER MENU IS ON TOP OF THE BROWSER. Right-click a user folder: the menu is a
//     body-level .pmenu, and document.elementFromPoint at its rows returns the MENU, not the
//     panel. Measured with the panel deliberately underneath it. The browser stays open.
//  8  FACTORY GETS NO "REMOVE FOLDER". A factory-tagged folder offers Locate only.
//  9  THE MENU CLAMPS. Opened at the bottom-right corner, it stays inside the viewport.
// 10  NO EMPTY CATEGORY, and the ten are the ten.
const puppeteer = require('puppeteer-core');
const fs = require('fs'), path = require('path');
const SRC = process.argv[2] || '/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/Terrain/Source/ui/public/index.html';
// ── MUTATION CONTROLS (fb421 — a gate that has never failed has never been tested). The page is
//    mutated AT SOURCE; a mutation the runtime could undo is no proof.
//      FB606_MUT=synmenu  put the folder menu back on the #syn-panel-scoped menu  → ON TOP fails
//      FB606_MUT=flat     make the relative-subfolder reader always return []     → the tree fails
//      FB606_MUT=empty    stop dropping categories with no tables in them         → NO EMPTY fails
const MUT = process.env.FB606_MUT || '';
const MUTS = {
  synmenu: [ 'if (mi.length) window.__tpbFolderMenu (cat.label, mi, e.clientX, e.clientY);',
             'if (mi.length && window.__synShowMenu) window.__synShowMenu (cat.label, mi, e.clientX, e.clientY);' ],
  flat:    [ '    function wtRelSegs (it, rootPath) {\n      var r = null;',
             '    function wtRelSegs (it, rootPath) {\n      return [];\n      var r = null;' ],
  empty:   [ "      if (items.length) cats.push ({ label: lab, items: items });",
             "      cats.push ({ label: lab, items: items });" ]
};
let PAGE = SRC;
if (MUT) {
  if (!MUTS[MUT]) { console.error('unknown FB606_MUT ' + MUT); process.exit(2); }
  const [from, to] = MUTS[MUT];
  let h = require('fs').readFileSync(SRC, 'utf8');
  if (h.split(from).length - 1 !== 1) { console.error('mutation anchor "' + MUT + '" hit ' + (h.split(from).length - 1) + ' times'); process.exit(2); }
  // 'empty' also needs a category that WOULD be empty: give __WT_FILING an extra, unfilled drawer
  if (MUT === 'empty') {
    h = h.replace("window.__WT_TEN = ['Basic Shapes'", "window.__WT_TEN = ['Ghost Drawer', 'Basic Shapes'")
         .replace("window.__WT_FILING = {\n    'Basic Shapes'", "window.__WT_FILING = {\n    'Ghost Drawer': [],\n    'Basic Shapes'")
         .replace("      if (items.length) wcats.push ({ label: c.label, kind: 'factory', items: items });",
                  "      wcats.push ({ label: c.label, kind: 'factory', items: items });");
  }
  PAGE = require('path').join(require('os').tmpdir(), 'fb606_mut_' + MUT + '.html');
  require('fs').writeFileSync(PAGE, h.split(from).join(to));
}
const VW = 820, VH = 656, DSF = 2;
let pass = 0, fail = 0;
const chk = (ok, label, detail) => { if (ok) { pass++; console.log('  ok    ' + label + (detail ? '\n          ' + detail : '')); }
  else { fail++; console.log('  FAIL  ' + label + (detail ? '\n          ' + detail : '')); } };
const settle = (ms) => new Promise((r) => setTimeout(r, ms));

// THE PAYLOAD the recursive C++ scan hands back. NOTE: no `rel`, no `sub`, no `kind` on the items —
// only what a plain recursive findChildFiles gives you, so the UI has to DERIVE the hierarchy.
const REG_WT = {
  folders: [
    { name: 'MASTER', path: '/u/MASTER', count: 7, items: [
      { name: 'Top Level A',  path: '/u/MASTER/Top Level A.wav' },
      { name: 'Bass Deep 1',  path: '/u/MASTER/Bass/Bass Deep 1.wav' },
      { name: 'Bass Deep 2',  path: '/u/MASTER/Bass/Bass Deep 2.wav' },
      { name: 'Sub Rumble',   path: '/u/MASTER/Bass/Sub/Sub Rumble.wav' },
      { name: 'Lead Screamer',path: '/u/MASTER/Leads/Lead Screamer.wav' },
      { name: 'Lead Glassy',  path: '/u/MASTER/Leads/Lead Glassy.wav' },
      { name: 'Pad Nimbus',   path: '/u/MASTER/Pads/Pad Nimbus.wav' } ] },
    // a FACTORY folder, tagged — merges into one of the ten, and must never offer Remove
    { name: 'Cinematic', path: '/factory/wt/Cinematic', kind: 'factory', count: 2, items: [
      { name: 'Bank Drone A', path: '/factory/wt/Cinematic/Bank Drone A.wav' },
      { name: 'Bank Drone B', path: '/factory/wt/Cinematic/Bank Drone B.wav' } ] }
  ],
  files: [ { name: 'Hand Drawn', path: '/u/Hand Drawn.wav' } ]
};

const WT_N = (() => { const s = fs.readFileSync(path.join(path.dirname(SRC), '../../../Source/PluginProcessor.cpp'), 'utf8');
  const a = s.indexOf('ParameterIDs::SYN_OSC_A_WT_PRESET, 1 }'); const b = s.indexOf('juce::StringArray {', a);
  const e = s.indexOf('},', b); return (s.slice(b, e).replace(/\/\/[^\n]*/g, '').match(/"(?:[^"\\]|\\.)*"/g) || []).length; })();

const STUB = (cfg) => {
  const CH = {}; ['A','B','C','D'].forEach((o) => { CH['SYN_OSC_'+o+'_WT_PRESET'] = cfg.wtN; CH['SYN_OSC_'+o+'_ENGINE'] = 7; });
  window.__natives = []; window.__reg = cfg.reg;
  const states = new Map();
  const mk = (name, n) => { const props = { start:0, end:(n?n-1:1), skew:1, name, label:'', numSteps:n||100, interval:n?1:0, parameterIndex:states.size };
    const st = { name, scaledValue:0, properties:props, getScaledValue:()=>st.scaledValue, setScaledValue(v){st.scaledValue=v;},
      getNormalisedValue(){ return (st.scaledValue-props.start)/((props.end-props.start)||1); },
      setNormalisedValue(v){ st.scaledValue = n ? Math.round(v*(props.end-props.start)) : v; (st.__ls||[]).forEach(f=>{try{f();}catch(e){}}); },
      valueChangedEvent:{addListener(f){(st.__ls=st.__ls||[]).push(f);return{remove(){}}},removeListener(){}},
      propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
      getChoiceIndex:()=>st.scaledValue, setChoiceIndex(i){st.scaledValue=i;}, getValue:()=>false, setValue(){},
      sliderDragStarted(){}, sliderDragEnded(){} }; return st; };
  const get = (nm) => { if (!states.has(nm)) states.set(nm, mk(nm, CH[nm])); return states.get(nm); };
  const nativeFn = (n) => (...a) => new Promise((r) => { window.__natives.push({ fn:n, args:a.map(String) });
    if (n === 'listWtImports') return r(JSON.stringify(window.__reg));
    if (n === 'listImports')   return r('[]');
    if (/getPresets/i.test(n)) return r('[]');
    if (/Json|JSON/.test(n))   return r('{}');
    r(0); });
  window.Juce = { getSliderState:get, getToggleState:get, getComboBoxState:get, getNativeFunction:nativeFn,
                  backend:{addEventListener(){},removeEventListener(){},emitEvent(){}} };
  // the page's own juce shim reassigns window.Juce on load; hold onto OUR two accessors through it
  (function(){ const mine = window.Juce; let held = mine; Object.defineProperty(window, 'Juce', { configurable:true,
    get(){ return held; }, set(v){ held = Object.assign({}, v||{}, { getNativeFunction:mine.getNativeFunction, getSliderState:mine.getSliderState }); } }); })();
  window.__JUCE__ = { backend:window.Juce.backend, initialisationData:{ vendor:'', pluginName:'', pluginVersion:'',
    __juce__sliders:[], __juce__toggles:[], __juce__comboBoxes:[], __juce__functions:[] } };
  Element.prototype.setPointerCapture = function(){};
  Element.prototype.releasePointerCapture = function(){};
};

const HELP = () => {
  window.__q  = (s) => document.querySelector(s);
  window.__panes = () => [...document.querySelectorAll('.tpb-pane')];
  window.__rows = (p) => [...p.children].map((d) => ({ el:d,
    name: (d.querySelector('span[style*="flex:1"]') || d).textContent.trim(),
    count: (() => { const ss=[...d.querySelectorAll('span')]; const s=ss[ss.length-1];
                    return (s && /^\d+$/.test(s.textContent.trim())) ? +s.textContent.trim() : null; })(),
    chev: !!d.querySelector('svg path[d^="M1.5 1.3"]') }));
  window.__cats  = () => { const p = window.__panes(); return p.length<2 ? null : window.__rows(p[0]); };
  window.__items = () => { const p = window.__panes(); return p.length<2 ? null : window.__rows(p[1]); };
  window.__crumb = () => { const c = document.querySelector('.tpb-crumb'); return c ? c.textContent.trim() : null; };
  window.__geom  = () => { const p = document.querySelector('.tpb-panel'); if (!p) return null; const r = p.getBoundingClientRect();
    const pn = window.__panes().map((e) => { const q = e.getBoundingClientRect(); return [q.width|0, q.height|0]; });
    return { x:Math.round(r.x), y:Math.round(r.y), w:Math.round(r.width), h:Math.round(r.height), panes:pn }; };
  window.__openWt = () => { try { if (window.__tpbClose) window.__tpbClose(); } catch(e){}
    [...document.querySelectorAll('#syn-panel .device.osc')].forEach((d) =>
      ['engine-sample','engine-granular','engine-geode','engine-harm','engine-modal','engine-fm'].forEach((c) => d.classList.remove(c)));
    window.openWtSelectMenu('a', { clientX:180, clientY:120, preventDefault(){}, stopPropagation(){} }); return 'opened'; };
  window.__clickCat = (nm) => { const c = (window.__cats()||[]).find((r) => r.name === nm); if (!c) return 'no cat ' + nm;
    c.el.click(); return 'clicked'; };
  // the REAL screen point of a category row, so puppeteer's own mouse can double-click it
  window.__catPt = (nm) => { const c = (window.__cats()||[]).find((r) => r.name === nm); if (!c) return null;
    c.el.scrollIntoView({ block:'center' });   // the folder column scrolls; click a row that is actually on screen
    const r = c.el.getBoundingClientRect(); return { x:Math.round(r.x + r.width/2), y:Math.round(r.y + r.height/2) }; };
  window.__navArrows = () => { const h = document.querySelector('.tpb-panel [title="Preview (headphone) — while on, click a sound to hear it"]');
    const b = document.querySelector('.tpb-panel [title="Back"]'), f = document.querySelector('.tpb-panel [title="Forward"]');
    const mid = (e) => { if (!e) return null; const r = e.getBoundingClientRect(); return { cy:+(r.y + r.height/2).toFixed(2), cx:+(r.x + r.width/2).toFixed(2), w:Math.round(r.width), h:Math.round(r.height) }; };
    return { back:mid(b), fwd:mid(f), hp:mid(h), backCol:b?getComputedStyle(b).color:null, fwdCol:f?getComputedStyle(f).color:null }; };
  window.__hitBack = () => { const b = document.querySelector('.tpb-panel [title="Back"]'); if (!b) return 'no back'; b.click(); return 'ok'; };
  window.__hitFwd  = () => { const f = document.querySelector('.tpb-panel [title="Forward"]'); if (!f) return 'no fwd'; f.click(); return 'ok'; };
  window.__search  = (q) => { const s = document.querySelector('.tpb-srch'); if (!s) return null; s.value = q; s.oninput();
    return (window.__items()||[]).map((r) => r.name); };
  // right-click a category row at a chosen screen point
  window.__ctx = (nm, x, y) => { const c = (window.__cats()||[]).find((r) => r.name === nm); if (!c) return 'no cat ' + nm;
    c.el.dispatchEvent(new MouseEvent('contextmenu', { bubbles:true, cancelable:true, clientX:x, clientY:y })); return 'ok'; };
  window.__menu = () => { const m = document.querySelector('.pmenu'); if (!m) return null;
    const r = m.getBoundingClientRect();
    const rows = [...m.querySelectorAll('.pi')].map((d) => d.textContent.trim());
    // 🚨 THE MEASUREMENT THAT MATTERS: what is actually PAINTED at the menu row's own pixel?
    const probes = [...m.querySelectorAll('.pi')].map((d) => { const q = d.getBoundingClientRect();
      const hit = document.elementFromPoint(Math.round(q.x + q.width/2), Math.round(q.y + q.height/2));
      return { row:d.textContent.trim(), hitInMenu: !!(hit && hit.closest && hit.closest('.pmenu')),
               hitTag: hit ? (hit.className && String(hit.className).slice(0,24)) || hit.tagName : 'null' }; });
    const panel = document.querySelector('.tpb-panel');
    const pr = panel ? panel.getBoundingClientRect() : null;
    return { rows, probes, rect:{ x:Math.round(r.x), y:Math.round(r.y), w:Math.round(r.width), h:Math.round(r.height) },
             overlapsPanel: !!(pr && r.x < pr.right && r.right > pr.x && r.y < pr.bottom && r.bottom > pr.y),
             zMenu:getComputedStyle(m).zIndex, zPanel:panel?getComputedStyle(panel).zIndex:null,
             menuAfterPanel: !!(panel && (panel.compareDocumentPosition(m) & Node.DOCUMENT_POSITION_FOLLOWING)),
             panelStillOpen: !!panel, vw:window.innerWidth, vh:window.innerHeight };
  };
};

(async () => {
  const b = await puppeteer.launch({ executablePath:(process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'),
    headless:'new', args:['--no-sandbox','--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width:VW, height:VH, deviceScaleFactor:DSF });
  const errs = []; pg.on('pageerror', (e) => errs.push(String(e).slice(0,180)));
  const logs = []; const alllogs = [];
  pg.on('console', (m) => { const t = m.text(); alllogs.push(m.type()+': '+t.slice(0,200)); if (/fb606/.test(t)) logs.push(t); });
  await pg.evaluateOnNewDocument(() => { window.addEventListener('unhandledrejection', (e) => console.error('UNHANDLED: ' + (e.reason && e.reason.stack ? e.reason.stack : e.reason))); });
  await pg.evaluateOnNewDocument(STUB, { reg:REG_WT, wtN:WT_N });
  await pg.evaluateOnNewDocument(HELP);
  await pg.goto('file://' + PAGE, { waitUntil:'load', timeout:60000 });
  await settle(1400);
  await pg.evaluate(() => { const sp = document.getElementById('syn-panel'); if (sp) sp.style.display='block'; window.dispatchEvent(new Event('resize')); });
  await settle(700);

  console.log('\n══ fb606 — folder navigation + the folder menu on top ══' + (MUT ? ('   [MUTATION ' + MUT + ']') : ''));
  console.log('   page ' + PAGE + '\n   viewport ' + VW + '×' + VH + '   WT_N ' + WT_N + '\n');

  const op = await pg.evaluate(() => { try { return window.__openWt(); } catch(e) { return 'threw: ' + String(e); } }); await settle(600);
  const diag = await pg.evaluate(() => ({ panel: !!document.querySelector('.tpb-panel'), panes: document.querySelectorAll('.tpb-pane').length,
      cascade: document.querySelectorAll('.samp-menu').length, sel: !!document.getElementById('osc-a-preset-select') }));
  console.log('   open=' + op + '  diag=' + JSON.stringify(diag));
  if (errs.length) { console.log('   early page errors: ' + JSON.stringify([...new Set(errs)])); }
  console.log('   console tail: ' + JSON.stringify(alllogs.slice(-8), null, 1));

  // ── 10 / 1 ──────────────────────────────────────────────────────────────────────────────────
  const cats = await pg.evaluate(() => window.__cats());
  const names = cats.map((c) => c.name);
  const TEN = ['Basic Shapes','Analog','Digital','Vocal','Metallic','Spectral','Chaos','Cinematic','Harmonic','Physical'];
  chk(TEN.every((t) => names.indexOf(t) >= 0) && names.indexOf('Experimental') < 0 && names.indexOf('Morph') < 0 && names.indexOf('Terra') < 0,
      'THE TEN — the merged taxonomy is what the left column shows (no Experimental, no Morph, no Terra)',
      '[' + names.join(' · ') + ']');
  const empties = cats.filter((c) => c.name !== 'All' && c.count === 0).map((c) => c.name);
  chk(empties.length === 0, 'NO EMPTY CATEGORY — "delete anything that doesn\'t have a table inside of it"',
      empties.length ? ('empty: ' + empties.join(', ')) : 'every rendered category has at least one table');
  const master = cats.find((c) => c.name === 'MASTER');
  chk(!!master && master.count === 7 && master.chev,
      'MASTER FOLDER — one category, EVERY table beneath it, and a chevron saying it has folders',
      master ? ('count ' + master.count + ' (7 files: 1 at top, 3 in Bass/, 2 in Leads/, 1 in Pads/) · chevron ' + master.chev) : 'no MASTER row');
  const cine = cats.find((c) => c.name === 'Cinematic');
  chk(!!cine && cine.count === 4,
      'FACTORY BANK MERGES INTO THE TEN — the tagged "Cinematic" folder joins the built-in Cinematic drawer',
      cine ? ('Cinematic count ' + cine.count + ' = 2 built-in (Terra Cloud, Terra Dust) + 2 from the bank') : 'no Cinematic row');

  const g0 = await pg.evaluate(() => window.__geom());
  const crumb0 = await pg.evaluate(() => window.__crumb());

  // ── 2  ONE CLICK DESCENDS ───────────────────────────────────────────────────────────────────
  await pg.evaluate(() => window.__clickCat('MASTER')); await settle(120);
  const l1cats = await pg.evaluate(() => window.__cats());
  const l1items = await pg.evaluate(() => window.__items());
  const crumb1 = await pg.evaluate(() => window.__crumb());
  chk(l1cats[0].name === 'All' && JSON.stringify(l1cats.map((c)=>c.name)) === JSON.stringify(['All','Bass','Leads','Pads']),
      'ONE CLICK DESCENDS — the left column becomes All + this folder\'s subfolders',
      '[' + l1cats.map((c) => c.name + '(' + c.count + ')').join(' · ') + ']');
  chk(l1items.length === 7,
      'VIEW-ALL-BELOW — landing inside MASTER shows every table under it, not just its direct children',
      l1items.length + ' rows: ' + l1items.map((r) => r.name).join(', '));
  chk(crumb1 === 'Wavetables  ›  MASTER' && crumb0 === 'Wavetables',
      'BREADCRUMB — it is never unclear where you are', 'root "' + crumb0 + '"  →  "' + crumb1 + '"');
  const g1 = await pg.evaluate(() => window.__geom());
  chk(JSON.stringify(g0) === JSON.stringify(g1),
      'FIXED POSITIONS — descending does not move or resize the panel or either pane by one pixel',
      'before ' + JSON.stringify(g0) + '\n          after  ' + JSON.stringify(g1));

  // deeper: Bass → Sub
  await pg.evaluate(() => window.__clickCat('Bass')); await settle(120);
  const l2 = await pg.evaluate(() => ({ cats:window.__cats().map((c)=>c.name+'('+c.count+')'), items:window.__items().length, crumb:window.__crumb() }));
  chk(l2.cats.join(' · ') === 'All(3) · Sub(1)' && l2.items === 3,
      'TWO LEVELS DEEP — Bass shows its own subtree (3 tables: 2 direct + 1 in Sub/)',
      '[' + l2.cats.join(' · ') + ']  items ' + l2.items + '  crumb "' + l2.crumb + '"');

  // ── 6  SEARCH SPANS THE SUBTREE ─────────────────────────────────────────────────────────────
  const hits = await pg.evaluate(() => window.__search('lead'));
  chk(hits && hits.length === 2 && hits.indexOf('Lead Screamer') >= 0,
      'SEARCH SPANS THE WHOLE SUBTREE — typed two folders deep inside Bass, it finds Leads/ tables',
      'query "lead" from Wavetables › MASTER › Bass → [' + (hits||[]).join(', ') + ']');
  const once = await pg.evaluate(() => window.__search('Sub Rumble'));
  chk(once && once.length === 1, 'SEARCH lists a nested hit EXACTLY ONCE (the master folder and its subfolder share the object)',
      'query "Sub Rumble" → ' + JSON.stringify(once));
  await pg.evaluate(() => window.__search(''));  await settle(80);

  // ── 4 / 5  BACK / FORWARD + CENTRELINE ──────────────────────────────────────────────────────
  const armed = await pg.evaluate(() => window.__navArrows());
  chk(armed.back && armed.hp && armed.back.cy === armed.hp.cy && armed.fwd.cy === armed.hp.cy,
      'CENTRELINE — the ‹ › glyph boxes sit on the header centreline with the headphones',
      'back cy ' + (armed.back&&armed.back.cy) + ' · fwd cy ' + (armed.fwd&&armed.fwd.cy) + ' · headphone cy ' + (armed.hp&&armed.hp.cy)
      + '   boxes ' + JSON.stringify([armed.back&&[armed.back.w,armed.back.h], armed.fwd&&[armed.fwd.w,armed.fwd.h]]));
  await pg.evaluate(() => window.__hitBack()); await settle(120);
  const bk1 = await pg.evaluate(() => ({ crumb:window.__crumb(), cats:window.__cats().map((c)=>c.name), geom:window.__geom(), arr:window.__navArrows() }));
  chk(bk1.crumb === 'Wavetables  ›  MASTER' && bk1.cats.join(',') === 'All,Bass,Leads,Pads',
      'BACK — ‹ walks the history one step out', '"' + bk1.crumb + '"  [' + bk1.cats.join(' · ') + ']');
  await pg.evaluate(() => window.__hitBack()); await settle(120);
  const bk2 = await pg.evaluate(() => ({ crumb:window.__crumb(), first:window.__cats()[0].name, geom:window.__geom(), arr:window.__navArrows() }));
  chk(bk2.crumb === 'Wavetables' && bk2.first === 'All', 'BACK — ‹ reaches the root', '"' + bk2.crumb + '"');
  chk(JSON.stringify(bk2.geom) === JSON.stringify(g0),
      'FIXED POSITIONS — two levels in and two back out, the panel geometry is identical', JSON.stringify(bk2.geom));
  chk(bk2.arr.back.cx === armed.back.cx && bk2.arr.back.cy === armed.back.cy && bk2.arr.back.w === armed.back.w,
      'CENTRELINE — a DISABLED back arrow is the same box in the same place (only the colour changed)',
      'enabled ' + JSON.stringify(armed.back) + ' ' + armed.backCol + '\n          disabled ' + JSON.stringify(bk2.arr.back) + ' ' + bk2.arr.backCol);
  await pg.evaluate(() => window.__hitFwd()); await pg.evaluate(() => window.__hitFwd()); await settle(140);
  const fw = await pg.evaluate(() => ({ crumb:window.__crumb(), items:window.__items().length }));
  chk(fw.crumb === 'Wavetables  ›  MASTER  ›  Bass' && fw.items === 3,
      'FORWARD — › walks back in, both steps', '"' + fw.crumb + '"  items ' + fw.items);

  // ── 3  DOUBLE-CLICK DESCENDS ONCE ───────────────────────────────────────────────────────────
  await pg.evaluate(() => { window.__hitBack(); window.__hitBack(); }); await settle(140);
  // a REAL double-click, driven by puppeteer's mouse: mousedown/up detail 1, mousedown/up detail 2,
  // and the browser's own dblclick — on whatever element is actually under the cursor each time.
  const pt = await pg.evaluate(() => window.__catPt('MASTER')); await settle(60);
  const inPanel = await pg.evaluate((p) => { const el = document.elementFromPoint(p.x, p.y);
    return !!(el && el.closest && el.closest('.tpb-panel')); }, pt);
  await pg.mouse.click(pt.x, pt.y, { clickCount:2 }); await settle(240);
  const dbl = await pg.evaluate(() => window.__crumb());
  chk(dbl === 'Wavetables  ›  MASTER',
      'DOUBLE-CLICK DESCENDS ONCE — a real 2-click gesture on MASTER lands ONE level in, not two',
      'puppeteer mouse.click(clickCount:2) at ' + JSON.stringify(pt) + ' (inside the panel: ' + inPanel + ') → "' + dbl + '"');

  // ── 7  THE FOLDER MENU IS ON TOP ────────────────────────────────────────────────────────────
  await pg.evaluate(() => { window.__hitBack(); }); await settle(140);
  await pg.evaluate(() => window.__ctx('MASTER', 300, 300)); await settle(120);
  const m = await pg.evaluate(() => window.__menu());
  chk(!!m && m.rows.length === 2 && m.rows.join('|') === 'Locate Folder|Remove Folder',
      'USER FOLDER — right-click offers Locate + Remove', m ? m.rows.join(' · ') : 'no .pmenu');
  chk(!!m && m.overlapsPanel && m.probes.every((p) => p.hitInMenu),
      '🚨 ON TOP — with the menu OVERLAPPING the browser, the pixel at every menu row belongs to the MENU',
      m ? ('overlaps panel ' + m.overlapsPanel + ' · elementFromPoint per row ' + JSON.stringify(m.probes)) : '—');
  chk(!!m && m.zMenu === m.zPanel && m.menuAfterPanel,
      'AND IT IS DOM ORDER THAT DID IT — same z-index, menu appended after the panel (raising z was never the fix)',
      m ? ('z menu ' + m.zMenu + ' · z panel ' + m.zPanel + ' · menu follows panel in the DOM: ' + m.menuAfterPanel) : '—');
  chk(!!m && m.panelStillOpen, 'THE BROWSER STAYS OPEN under its own menu', m ? ('.tpb-panel present: ' + m.panelStillOpen) : '—');

  // ── 8  FACTORY GETS NO REMOVE ───────────────────────────────────────────────────────────────
  await pg.evaluate(() => { if (window.__tpbMenuClose) window.__tpbMenuClose(); });
  await pg.evaluate(() => window.__ctx('Cinematic', 300, 300)); await settle(120);
  const mf = await pg.evaluate(() => window.__menu());
  chk(!!mf && mf.rows.indexOf('Locate Folder') >= 0 && mf.rows.indexOf('Remove Folder') < 0,
      'FACTORY — the drawer can be LOCATED but "Remove Folder" is NEVER offered on it',
      mf ? ('rows: ' + JSON.stringify(mf.rows)) : 'no menu at all');

  // ── 9  THE CLAMP ────────────────────────────────────────────────────────────────────────────
  await pg.evaluate(() => { if (window.__tpbMenuClose) window.__tpbMenuClose(); });
  await pg.evaluate(() => window.__ctx('MASTER', 818, 654)); await settle(120);
  const mc = await pg.evaluate(() => window.__menu());
  chk(!!mc && mc.rect.x >= 6 && mc.rect.y >= 6 && (mc.rect.x + mc.rect.w) <= mc.vw - 6 && (mc.rect.y + mc.rect.h) <= mc.vh - 6,
      'MENUS NEVER CUT OFF — opened at the bottom-right corner (818,654) it clamps inside the viewport',
      mc ? ('rect ' + JSON.stringify(mc.rect) + ' in ' + mc.vw + '×' + mc.vh) : 'no menu');

  // ── 11  CPU — "A folder with hundreds of files is the normal case now." MEASURED, not asserted.
  await pg.evaluate(() => { if (window.__tpbMenuClose) window.__tpbMenuClose(); if (window.__tpbClose) window.__tpbClose(); });
  const stress = await pg.evaluate(() => {
    const items = []; const SUB = ['Bass','Leads','Pads','Keys','FX','Perc','Drones','Vox','Metal','Glass','Dust','Wind'];
    for (let i = 0; i < 480; i++) { const d = SUB[i % SUB.length];
      items.push({ name:'Table ' + i, path:'/u/BIG/' + d + '/' + (i % 3 === 0 ? 'Deep/' : '') + 'Table ' + i + '.wav' }); }
    window.__reg = { folders:[{ name:'BIG', path:'/u/BIG', count:items.length, items:items }], files:[] };
    const t0 = performance.now();
    window.__openWt();
    return { built:+(performance.now() - t0).toFixed(1), n:items.length };
  });
  await settle(500);
  const st2 = await pg.evaluate(() => {
    const cats = window.__cats(); const big = cats.find((c) => c.name === 'BIG');
    const t0 = performance.now(); big.el.click(); const desc = performance.now() - t0;
    const t1 = performance.now(); const s = document.querySelector('.tpb-srch'); s.value = 'table 4'; s.oninput(); const srch = performance.now() - t1;
    s.value = ''; s.oninput();
    return { bigCount:big.count, subs:window.__cats().length - 1, rows:window.__items().length,
             descendMs:+desc.toFixed(1), searchMs:+srch.toFixed(1), geom:window.__geom() };
  });
  chk(st2.bigCount === 480 && st2.descendMs < 120 && st2.searchMs < 120,
      'CPU — 480 tables across 12 subfolders: the tree builds, descends and searches without stalling the WebView',
      'BIG count ' + st2.bigCount + ' · ' + st2.subs + ' subfolders · descend ' + st2.descendMs + ' ms · '
      + 'search over the whole subtree ' + st2.searchMs + ' ms · open+scan ' + stress.built + ' ms (synchronous JS only; no native call, no audio thread)');
  chk(st2.geom.w === 384 && st2.geom.h === 342,
      'FIXED POSITIONS — 480 rows do not resize the panel by a pixel', JSON.stringify(st2.geom));

  console.log('\n  console (the detectors that must print whether they fired):');
  [...new Set(logs)].forEach((l) => console.log('    ' + l));
  if (errs.length) { console.log('\n  page errors:'); [...new Set(errs)].forEach((e) => console.log('    ' + e)); }
  console.log('\n  ' + pass + ' passed, ' + fail + ' failed' + (MUT ? ('   [MUTATION ' + MUT + ' — failures here are the PROOF]') : ''));
  await b.close();
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
