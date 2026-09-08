// ══════════════════════════════════════════════════════════════════════════════════════════════
//  macro_rename_gate.js — fb574: A MACRO'S NAME CAN BE RENAMED — FROM THE MENU AND BY DOUBLE-CLICK — IN THE HOST.
//
//    NODE_PATH=<scratchpad>/node_modules node Tests/macro_rename_gate.js [page.html]
//
//  Max (2026-09-03): "I'm trying to right-click, press rename, and it's not working. Could you please fix
//  this or make it so that we can double-click on the name... the letters have to be the same size as
//  everything else."
//
//  WHY (read, then measured on the installed AU): opening the field arms the fb135 host-key bridge, and
//  the C++ side of that (editArm) calls grabKeyboardFocus() — JUCE takes keyboard focus from the WKWebView,
//  WebKit fires `blur` on the field, and the field's own blur handler committed and CLOSED it before a
//  key could land. The fb136 preset rename already knew ("survive the arming blur") and has no blur
//  handler; the macro field was the odd one out. The field was also 7 px under an 8 px label.
//
//  THE BARS
//   1  THE MENU PATH — a real right-click on the name → the one menu → its Rename row → the field opens,
//      registered on the bridge, and the menu is gone.
//   2  SURVIVES THE ARMING BLUR — the field is blurred (what WebKit does when JUCE grabs focus): it stays,
//      and stays registered on the bridge.
//   3  TYPES THROUGH THE BRIDGE — B·a·b·y·Enter via __tiHostKey → the name is "Baby" on the label and in
//      __macroName; the field is gone.
//   4  DOUBLE-CLICK RENAMES — a real double-click on the name opens the field; C·u·t·Enter → "Cut"; no menu.
//   5  A CLICK ELSEWHERE COMMITS, ESCAPE REVERTS.
//   6  THE FIELD'S TYPE IS THE LABEL'S — one font, one size (8 px, the synth page's knob word), one spacing.
//   7  A REPAINT OF THE NAMES LEAVES AN OPEN FIELD ALONE — measured on the installed AU: the field died at
//      +400 ms with NO blur; the late names seed (paintNames) had rewritten the word over it. Now another
//      macro's rename repaints every word but the one being typed into.
//
//  PROOF THE BARS CAN FAIL:  MACRO_RENAME_MUTATE=1 (blur → commit is back)  → 2, 3 red
//                            MACRO_RENAME_MUTATE=2 (the field back to 7 px)  → 6 red
//                            MACRO_RENAME_MUTATE=3 (no double-click branch)  → 4, 5, 6, 7 red
//                            MACRO_RENAME_MUTATE=4 (paintNames over the field) → 7 red
// ══════════════════════════════════════════════════════════════════════════════════════════════

const puppeteer = require('puppeteer-core');
const fs = require('fs'), path = require('path'), os = require('os');
const ROOT = path.join(__dirname, '..');
const PAGE = process.argv[2] || path.join(ROOT, 'Source/ui/public/index.html');
let pass = 0, fail = 0;
const chk = (ok, l, d) => { if (ok) { pass++; console.log('  ok    ' + l + (d ? '   ' + d : '')); } else { fail++; console.log('  FAIL  ' + l + (d ? '   ' + d : '')); } };
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
const STUB = () => { const mk = () => ({getScaledValue:()=>0.5,setScaledValue(){},getNormalisedValue:()=>0.5,setNormalisedValue(){},getChoiceIndex:()=>0,setChoiceIndex(){},getValue:()=>false,setValue(){},
    valueChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},propertiesChangedEvent:{addListener(){return{remove(){}}},removeListener(){}},
    properties:{start:0,end:1,interval:0,name:'',label:'',numSteps:100,choices:[],parameterIndex:0}});
  window.Juce = {getSliderState:mk,getToggleState:mk,getComboBoxState:mk,getNativeFunction:(n)=>(...a)=>new Promise(r=>{ if(/getPresets/i.test(n))return r('[]'); if(/Json|JSON/.test(n))return r('{}'); r(0);}),backend:{addEventListener(){},removeEventListener(){},emitEvent(){}}};
  (function(){const mine=window.Juce;let held=mine;Object.defineProperty(window,'Juce',{configurable:true,get(){return held;},set(v){held=Object.assign({},v||{},{getNativeFunction:mine.getNativeFunction});}});})();
  window.__JUCE__={backend:window.Juce.backend,initialisationData:{vendor:'',pluginName:'',pluginVersion:'',__juce__sliders:[],__juce__toggles:[],__juce__comboBoxes:[],__juce__functions:[]}}; };
async function boot (P) {
  const b = await puppeteer.launch({ executablePath:(process.env.CHROME_PATH||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'), headless:'new', args:['--no-sandbox','--allow-file-access-from-files'] });
  const pg = await b.newPage(); await pg.setViewport({ width:820, height:656, deviceScaleFactor:2 });   /* the host's own page box: 820 wide, 656 inner (fb570's vis_diag on the installed AU) */
  const errs = []; pg.on('pageerror', e => errs.push(String(e).slice(0, 160)));
  await pg.evaluateOnNewDocument(STUB);
  await pg.goto('file://' + P, { waitUntil:'load', timeout:60000 }); await sleep(1800);
  await pg.evaluate(() => { document.documentElement.classList.remove('card-only-late'); document.querySelectorAll('.ti-preboot').forEach(e => e.classList.remove('ti-preboot'));
    const sp = document.getElementById('syn-panel'); sp.classList.remove('hidden'); sp.style.display = 'block'; try { document.getElementById('syn-btn').click(); } catch(e){} dispatchEvent(new Event('resize')); });
  await sleep(600);
  return { b, pg, errs };
}

const MUT = +(process.env.MACRO_RENAME_MUTATE || 0);
function mutatedPage () {
  if (! MUT) return PAGE;
  let src = fs.readFileSync(PAGE, 'utf8');
  const sub = (f, t) => { const n = src.split(f).length - 1; if (n !== 1) { console.error('MUTATION ' + MUT + ': anchor matched ' + n + ' times -> ' + f.slice(0, 90)); process.exit(2); } src = src.replace(f, t); console.log('   mutation ' + MUT + ' applied'); };
  if (MUT === 1) sub("function out(ev){ if(!lab.contains(ev.target)) done(true); }   /* fb574 */", "function out(ev){ if(!lab.contains(ev.target)) done(true); } inp.addEventListener('blur',function(){ setTimeout(function(){ done(true); },0); });   /* fb574 */");
  if (MUT === 2) sub("#syn-panel .vm-macro .vm-mi{ width:100%; box-sizing:border-box; font:inherit; font-size:8px;", "#syn-panel .vm-macro .vm-mi{ width:100%; box-sizing:border-box; font:inherit; font-size:7px;");
  if (MUT === 3) sub("var ml=e.target.closest('.vm-ml'); if(ml){", "var ml=null; if(ml){");
  if (MUT === 4) sub("if(mls[i]&&!mls[i].querySelector('input')) mls[i].textContent=window.__macroName(i+1);", "if(mls[i]) mls[i].textContent=window.__macroName(i+1);");
  const p = path.join(os.tmpdir(), 'macro_rename_mut' + MUT + '.html'); fs.writeFileSync(p, src); return p;
}

(async () => {
  const P = mutatedPage();
  const { b, pg, errs } = await boot(P);
  console.log('\n══ fb574 — A MACRO CAN BE RENAMED: THE MENU, THE DOUBLE-CLICK, THE BRIDGE ══');
  console.log('   page ' + P + (MUT ? '   MUTATION ' + MUT : '') + '\n');
  await pg.click('#syn-panel .vm-macros-btn'); await sleep(300);
  const lab = k => pg.evaluate(k => { const l = document.querySelectorAll('#syn-panel .vm-ml')[k - 1]; const r = l.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; }, k);
  const dblClick = p => pg.mouse.click(p.x, p.y, { count: 2 });   /* puppeteer-core 25: count = two presses (clickCount 1 then 2) → Chromium emits a real dblclick */
  const selAll = "const i=document.querySelector('#syn-panel .vm-ml .vm-mi'); if(!i) return {present:false}; try{ i.setSelectionRange(0, i.value.length); }catch(e){}";
  // 1 — the menu path
  let p = await lab(2); await pg.mouse.click(p.x, p.y, { button: 'right' }); await sleep(250);
  const row = await pg.evaluate(() => { const m = document.querySelector('.syn-ctx-menu.act'); if (!m) return { menu: false };
    const items = [].slice.call(m.querySelectorAll('.syn-ctx-item')); const it = items.filter(d => /^\s*Rename\s*$/.test(d.textContent))[0];
    if (!it) return { menu: true, rows: items.map(d => d.textContent.trim()).join(' · ') }; const r = it.getBoundingClientRect(); return { menu: true, x: r.left + r.width / 2, y: r.top + r.height / 2 }; });
  let field = { present: false };
  if (row.x) { await pg.mouse.click(row.x, row.y); await sleep(120);
    field = await pg.evaluate(() => { const i = document.querySelector('#syn-panel .vm-ml .vm-mi'); return { present: !!i, bridge: !!i && window.__tiActiveInp === i, menuOpen: !!document.querySelector('.syn-ctx-menu.act'), value: i ? i.value : null }; }); }
  chk(row.menu && !!row.x && field.present && field.bridge && !field.menuOpen, '1  THE MENU PATH: right-click the name → Rename → the field opens on the bridge, the menu closes', JSON.stringify({ menu: row.menu, rows: row.rows, field }));
  // 2 — the arming blur
  await pg.evaluate(() => { const i = document.querySelector('#syn-panel .vm-mi'); if (i) i.blur(); }); await sleep(80);
  const after = await pg.evaluate(() => { const i = document.querySelector('#syn-panel .vm-ml .vm-mi'); return { present: !!i, bridge: !!i && window.__tiActiveInp === i, label: document.querySelectorAll('#syn-panel .vm-ml')[1].textContent }; });
  chk(after.present && after.bridge, '2  SURVIVES THE ARMING BLUR: JUCE grabs keyboard focus when the bridge arms and WebKit blurs the field — it must stay, registered', JSON.stringify(after));
  // 3 — typing through the bridge
  const typed = await pg.evaluate(new Function(selAll + " ['B','a','b','y'].forEach(k => window.__tiHostKey(k)); const mid = i.value; window.__tiHostKey('Enter'); return { present: true, mid, name: window.__macroName(2), label: document.querySelectorAll('#syn-panel .vm-ml')[1].textContent, fieldLeft: !!document.querySelector('#syn-panel .vm-mi') };"));
  chk(typed.present && typed.mid === 'Baby' && typed.name === 'Baby' && typed.label === 'Baby' && !typed.fieldLeft, '3  TYPES THROUGH THE BRIDGE: B·a·b·y·Enter via __tiHostKey → "Baby" on the label and in __macroName, the field gone', JSON.stringify(typed));
  // 4 — double-click
  p = await lab(3); await dblClick(p); await sleep(150);
  const dbl = await pg.evaluate(new Function(selAll + " ['C','u','t'].forEach(k => window.__tiHostKey(k)); window.__tiHostKey('Enter'); return { present: true, name: window.__macroName(3), menuOpen: !!document.querySelector('.syn-ctx-menu.act') };"));
  chk(dbl.present && dbl.name === 'Cut' && !dbl.menuOpen, '4  DOUBLE-CLICK RENAMES: a real double-click on the name opens the field; C·u·t·Enter → "Cut"; no menu opened', JSON.stringify(dbl));
  // 5 — click elsewhere commits, Escape reverts
  p = await lab(4); await dblClick(p); await sleep(150);
  const out = await pg.evaluate(new Function(selAll + " window.__tiHostKey('X'); document.body.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, clientX: 400, clientY: 20 })); return { present: true, name: window.__macroName(4), fieldLeft: !!document.querySelector('#syn-panel .vm-mi') };"));
  await sleep(50); p = await lab(4); await dblClick(p); await sleep(150);
  const esc = await pg.evaluate(new Function(selAll + " window.__tiHostKey('Y'); const mid = i.value; window.__tiHostKey('Escape'); return { present: true, mid, name: window.__macroName(4), fieldLeft: !!document.querySelector('#syn-panel .vm-mi') };"));
  chk(out.present && out.name === 'X' && !out.fieldLeft && esc.present && esc.mid === 'Y' && esc.name === 'X' && !esc.fieldLeft, '5  A CLICK ELSEWHERE COMMITS ("X"), ESCAPE REVERTS (typed "Y", kept "X")', JSON.stringify({ out, esc }));
  // 6 — the type
  p = await lab(5); await dblClick(p); await sleep(150);
  const typ = await pg.evaluate(() => { const i = document.querySelector('#syn-panel .vm-ml .vm-mi'), l = document.querySelectorAll('#syn-panel .vm-ml')[0], k = document.querySelector('#syn-panel .knob-label'); if (!i) return { present: false };
    const a = getComputedStyle(i), bb = getComputedStyle(l), c = getComputedStyle(k);
    const out = { present: true, field: a.fontSize + ' ' + a.fontFamily.split(',')[0] + ' ls ' + a.letterSpacing, label: bb.fontSize + ' ' + bb.fontFamily.split(',')[0] + ' ls ' + bb.letterSpacing, knob: c.fontSize };   /* read BEFORE Escape detaches the field (a computed style is live) */
    window.__tiHostKey('Escape'); return out; });
  chk(typ.present && typ.field === typ.label && typ.label.indexOf(typ.knob) === 0, '6  THE FIELD\'S TYPE IS THE LABEL\'S: one font, one size (8 px, the knob word), one letter-spacing', JSON.stringify(typ));
  // 7 — a repaint of the names while a field is open
  p = await lab(6); await dblClick(p); await sleep(150);
  const rep = await pg.evaluate(new Function(selAll + " window.__macroRename(7, 'Zed'); const i2 = document.querySelector('#syn-panel .vm-ml .vm-mi'); const alive = !!i2 && i2 === i && window.__tiActiveInp === i; if (alive) { ['S','i','x'].forEach(k => window.__tiHostKey(k)); window.__tiHostKey('Enter'); } return { present: true, alive, six: window.__macroName(6), seven: window.__macroName(7), fieldLeft: !!document.querySelector('#syn-panel .vm-mi') };"));
  chk(rep.present && rep.alive && rep.six === 'Six' && rep.seven === 'Zed' && !rep.fieldLeft, '7  A REPAINT OF THE NAMES LEAVES AN OPEN FIELD ALONE: macro 7 renamed underneath → the field on 6 stays on the bridge, S·i·x·Enter → "Six"', JSON.stringify(rep));
  if (errs.length) console.log('   page errors: ' + errs.join(' | '));
  console.log(`\n══ RESULT: ${pass} pass, ${fail} FAIL ══`);
  await b.close(); process.exit(fail ? 1 : 0);
})().catch(e => { console.error('HARNESS ERROR', e); process.exit(2); });
