// ══════════════════════════════════════════════════════════════════════════════════════════════
//  macro_rename_gate.js — fb569: RIGHT-CLICK A MACRO -> RENAME, every time (not 1 in 10).
//
//    NODE_PATH=<node_modules> node Tests/macro_rename_gate.js [page.html]
//
//  Max: "whenever I right click on the macro to press rename it works probably one out of ten
//  times." ROOT CAUSE: the macro NAME is a drag grip (data-drag-macro) whose pointerdown handler
//  did not check the mouse button. A right-click armed the drag; moving the cursor to the menu to
//  click Rename crossed the 5 px threshold and started a phantom macro drag, and its pointerup
//  consumed the Rename click. FIX: every mod-drag grip is LEFT-button only.
//
//  THE BARS
//   1  RIGHT-BUTTON on a macro name + a 20 px move starts NO drag (no .sm-ghost, no .sm-dragging).
//   2  LEFT-BUTTON on a macro name + a 20 px move DOES start the drag (the feature still works),
//      and pointerup cleans it up.
//   3  A real contextmenu on a macro opens the house menu carrying a "Rename" row.
//   4  Clicking "Rename" puts an <input> into that macro's label (rename mode actually entered).
//
//  PROOF IT CAN FAIL: MRG_MUTATE=1 removes the macro grip's button guard -> bar 1 red.
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core');
const fs = require('fs'), path = require('path'), os = require('os');
const ROOT = path.join(__dirname, '..');
const MUT = +(process.env.MRG_MUTATE || 0);
let PAGE = process.argv[2] || path.join(ROOT, 'Source/ui/public/index.html');
const VW = 820, VH = 656;
let pass = 0, fail = 0;
const chk = (ok, l, d) => { (ok ? pass++ : fail++); console.log('  ' + (ok ? 'ok  ' : 'FAIL') + '  ' + l + (d ? '   ' + d : '')); };
const sleep = ms => new Promise(r => setTimeout(r, ms));

if (MUT === 1) {
  let src = fs.readFileSync(PAGE, 'utf8');
  const a = "if(e.button)return;   /* fb569 — LEFT button only: a right-click on a macro must open the menu (Rename), never arm a drag whose phantom move then eats the Rename click */ var t=e.target.closest&&e.target.closest('#syn-panel [data-drag-macro]');";
  if (src.split(a).length - 1 !== 1) { console.error('MUT1 anchor miss'); process.exit(2); }
  src = src.replace(a, "var t=e.target.closest&&e.target.closest('#syn-panel [data-drag-macro]');");
  PAGE = path.join(os.tmpdir(), 'mrg_mut1.html'); fs.writeFileSync(PAGE, src); console.log('  (mutation 1 landed)');
}

const STUB = () => {
  const states = new Map();
  const mk = (name) => { const st = { name, norm:0, properties:{ start:0,end:1,skew:1,name,label:'',numSteps:100,interval:0,parameterIndex:states.size },
    get scaledValue(){ return st.norm; }, getScaledValue(){ return st.norm; }, setScaledValue(v){ st.norm=v; },
    getNormalisedValue(){ return st.norm; }, setNormalisedValue(v){ st.norm=v; (st.__ls||[]).forEach(f=>{try{f();}catch(e){}}); },
    valueChangedEvent:{ addListener(f){ (st.__ls=st.__ls||[]).push(f); return {remove(){}}; }, removeListener(){} },
    propertiesChangedEvent:{ addListener(){ return {remove(){}}; }, removeListener(){} },
    getChoiceIndex(){ return 0; }, setChoiceIndex(){}, getValue:()=>false, setValue(){}, sliderDragStarted(){}, sliderDragEnded(){} }; return st; };
  const get = (n) => { if (!states.has(n)) states.set(n, mk(n)); return states.get(n); };
  const nf = (nm) => (...a) => new Promise(r => { if (/getMacroNames/.test(nm)) return r('[]'); if (/getPresets/i.test(nm)) return r('[]'); if (/getSynthMod$/.test(nm)) return r('[]'); if (/^getSynParam$/.test(nm)) return r(String(get(String(a[0])).getNormalisedValue())); if (/Json|JSON/.test(nm)) return r('{}'); r(0); });
  window.Juce = { getSliderState:get, getToggleState:get, getComboBoxState:get, getNativeFunction:nf, backend:{ addEventListener(){}, removeEventListener(){}, emitEvent(){} } };
  (function(){ const mine = window.Juce; let held = mine; Object.defineProperty(window,'Juce',{ configurable:true, get(){return held;}, set(v){ held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction,getSliderState:mine.getSliderState}); } }); })();
  window.__JUCE__ = { backend:window.Juce.backend, initialisationData:{ vendor:'',pluginName:'',pluginVersion:'',__juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[] } };
  Element.prototype.setPointerCapture = function(){}; Element.prototype.releasePointerCapture = function(){};
  window.__errs=[]; window.addEventListener('error',e=>{ try{ window.__errs.push(String(e.message).slice(0,120)); }catch(x){} });
};

(async () => {
  const b = await puppeteer.launch({ executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'), headless:'new', args:['--no-sandbox','--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width:VW, height:VH, deviceScaleFactor:2 });
  await pg.evaluateOnNewDocument(STUB);
  await pg.goto('file://' + PAGE, { waitUntil:'load', timeout:60000 }); await sleep(2400);
  await pg.evaluate(() => { document.documentElement.classList.remove('card-only-late');
    document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot'));
    const sp = document.getElementById('syn-panel'); if (sp) { sp.classList.remove('hidden'); sp.style.display='block'; }
    try { document.getElementById('syn-btn').click(); } catch(e){}
    // reveal the Macros view
    try { const mb=[...document.querySelectorAll('#syn-panel *')].find(e=>/^macros?$/i.test((e.textContent||'').trim())&&e.offsetParent); if(mb) mb.click(); } catch(e){}
    dispatchEvent(new Event('resize')); });
  await sleep(900);

  console.log('\n══ fb569 — RIGHT-CLICK A MACRO -> RENAME (every time) ══' + (MUT?'   MUTATION '+MUT:'') + '\n');

  const hasMacro = await pg.evaluate(() => !!document.querySelector('#syn-panel [data-drag-macro]'));
  chk(hasMacro, '0  the Macros view exposes a macro name grip');
  if (!hasMacro) { console.log('\n══ RESULT: ' + pass + ' pass, ' + fail + ' FAIL ══'); await b.close(); process.exit(1); }

  const geom = await pg.evaluate(() => { const el = document.querySelector('#syn-panel [data-drag-macro]'); const r = el.getBoundingClientRect(); return { x: r.left + r.width/2, y: r.top + r.height/2 }; });

  // ── 1 · RIGHT button + move → no drag ──
  const rc = await pg.evaluate(async (g) => {
    const el = document.querySelector('#syn-panel [data-drag-macro]');
    const pd = (x,y,btn) => el.dispatchEvent(new PointerEvent('pointerdown',{bubbles:true,cancelable:true,clientX:x,clientY:y,button:btn,buttons:btn===2?2:1}));
    const mv = (x,y) => document.dispatchEvent(new PointerEvent('pointermove',{bubbles:true,clientX:x,clientY:y}));
    const up = (x,y) => document.dispatchEvent(new PointerEvent('pointerup',{bubbles:true,clientX:x,clientY:y}));
    pd(g.x, g.y, 2); mv(g.x+20, g.y+20); mv(g.x+40, g.y+30);
    const dragging = document.body.classList.contains('sm-dragging') || !!document.querySelector('.sm-ghost');
    up(g.x+40, g.y+30);
    return dragging;
  }, geom);
  chk(!rc, '1  RIGHT-button + 20 px move starts NO drag (the guard holds)', rc ? 'a phantom drag started' : 'no ghost');

  // ── 2 · LEFT button + move → drag starts, then cleans up ──
  const lc = await pg.evaluate(async (g) => {
    const el = document.querySelector('#syn-panel [data-drag-macro]');
    const pd = (x,y,btn) => el.dispatchEvent(new PointerEvent('pointerdown',{bubbles:true,cancelable:true,clientX:x,clientY:y,button:btn,buttons:1}));
    const mv = (x,y) => document.dispatchEvent(new PointerEvent('pointermove',{bubbles:true,clientX:x,clientY:y}));
    const up = (x,y) => document.dispatchEvent(new PointerEvent('pointerup',{bubbles:true,clientX:x,clientY:y}));
    pd(g.x, g.y, 0); mv(g.x+20, g.y+20); mv(g.x+40, g.y+30);
    const dragging = document.body.classList.contains('sm-dragging') || !!document.querySelector('.sm-ghost');
    up(g.x+40, g.y+30);
    const cleaned = !document.body.classList.contains('sm-dragging') && !document.querySelector('.sm-ghost');
    return { dragging, cleaned };
  }, geom);
  chk(lc.dragging && lc.cleaned, '2  LEFT-button + 20 px move DOES drag (feature intact), and cleans up on release',
      'dragging=' + lc.dragging + ' cleaned=' + lc.cleaned);

  // ── 3 · contextmenu on the macro → a Rename row ──
  const menu = await pg.evaluate(async (g) => {
    const el = document.querySelector('#syn-panel [data-drag-macro]');
    el.dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,cancelable:true,clientX:g.x,clientY:g.y,button:2}));
    await new Promise(r=>setTimeout(r,60));
    const m = document.getElementById('syn-ctx-menu');
    const rows = m ? [...m.querySelectorAll('.syn-ctx-lab')].map(e=>e.textContent.trim()) : [];
    return { open: !!(m && m.classList.contains('act')), rows };
  }, geom);
  chk(menu.open && menu.rows.some(r => /rename/i.test(r)), '3  right-click a macro opens the house menu with a Rename row', JSON.stringify(menu.rows));

  // ── 4 · the Rename row IS wired to rename mode: the menu carries it (bar 3) and firing the
  //   rename entry (the row's own onPick target) puts an input in the macro label. A synthetic
  //   click on a menu row is not faithfully dispatched headless, so this exercises the mechanism
  //   the row calls; the button guard (bars 1-2) is what made the click reach it for a real hand.
  const renamed = await pg.evaluate(async () => {
    if (typeof window.__macroRenameOpen !== 'function') return false;
    try { window.__macroRenameOpen(1); } catch(e) { return false; }
    await new Promise(r=>setTimeout(r,50));
    return !!document.querySelector('#syn-panel .vm-ml input, #syn-panel .vm-macro input');
  });
  chk(renamed, '4  Rename enters rename mode: an input appears in the macro label');

  const errs = await pg.evaluate(() => window.__errs || []);
  chk(errs.length === 0, '5  no page errors', errs.join(' | '));

  console.log('\n══ RESULT: ' + pass + ' pass, ' + fail + ' FAIL ══\n');
  await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
